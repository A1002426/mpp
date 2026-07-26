#include"m_thread.h"
#include <unistd.h>     /* dup() */




void *thread_v4l2(void *argv)
{
    AQueue *q= (AQueue*)argv;
    DmaBuffer dma_bufs[BUF_NUM] = {0};
    const char *v4l2_dev = "/dev/video9";
    int fd = open(v4l2_dev, O_RDWR);
    q->fd=fd;
    if(fd < 0)
    {
        perror("open v4l2 device");
        frame_queue_stop(q->input_q);
        return NULL;
    }
    if(!capability(fd))
    {
        close(fd);
        frame_queue_stop(q->input_q);
        return NULL;
    }
    capfmt fmt_li={0};
    fs fs_li={0};
    fi fi_li={0};
    fmt_li.pixelformat=V4L2_PIX_FMT_MJPEG;
    fs_li.height=HEIGHT;
    fs_li.width=WIDTH;
    fi_li.numerator=1;
    fi_li.denominator=DENOMINATOR;
    if(!set_fmt(fd, &fmt_li, &fs_li))
    {
        close(fd);
        frame_queue_stop(q->input_q);
        return NULL;
    }
    if(!set_streamparm(fd, &fi_li))
    {
        close(fd);
        frame_queue_stop(q->input_q);
        return NULL;
    }
    if (!qbuf(fd, dma_bufs))
    {
        close(fd);
        frame_queue_stop(q->input_q);
        return NULL;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd, VIDIOC_STREAMON, &type) < 0)
    {
        perror("VIDIOC_STREAMON");  
        close(fd);
        frame_queue_stop(q->input_q);
        return NULL;
    }
    FILE *fp = fopen("frame.mjpg", "wb");
    if(!fp)    {
        perror("fopen");
        close(fd);
        frame_queue_stop(q->input_q);
        return NULL;
    }
    while(g_running)
    {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_DMABUF;
        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0)
        {
            perror("VIDIOC_DQBUF");
            break;
        }
        dma_bufs[buf.index].reallen=buf.bytesused;
        dma_bufs[buf.index].mxlen=buf.length;
        dma_bufs[buf.index].buf_index=buf.index;
        if(!dma_bufs[buf.index].start)
        {
            buf_mmap(&dma_bufs[buf.index]);
        }
        fwrite(dma_bufs[buf.index].start, 1,buf.bytesused, fp);
        if(queue_push(q->input_q,&dma_bufs[buf.index])<0)
        {
            break;
            
        }
    }
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    fclose(fp);
    close(fd);
    dma_bufs_release(dma_bufs,BUF_NUM);
    frame_queue_stop(q->input_q);
    return NULL;
}
/* 将 dmabuf 归还给 v4l2 */
static void qbuf_to_v4l2(AQueue *q, DmaBuffer pkt)
{
    struct v4l2_buffer buf = {0};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.index  = pkt.buf_index;
    buf.m.fd   = pkt.dmabuf;
    if (ioctl(q->fd, VIDIOC_QBUF, &buf) < 0)
        perror("QBUF dmabuf");
}

void *thread_mpp_dec(void *argv)
{
    AQueue *q = (AQueue*)argv;
    MppCtx ctx = NULL; 
    MppApi *mpi = NULL;
    MPP_RET ret;

    ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) {
        fprintf(stderr, "[ERR] mpp_create dec fail %d\n", ret);
        frame_queue_stop(q->input_q);
        return NULL;
    }

    ret = mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        fprintf(stderr, "[ERR] mpp_init dec mjpeg fail %d\n", ret);
        mpp_destroy(ctx);
        frame_queue_stop(q->input_q);
        return NULL;
    }

    /* 设置 split_parse，MJPEG 需要用 1 */
    MppDecCfg cfg = NULL;
    mpp_dec_cfg_init(&cfg);
    mpi->control(ctx, MPP_DEC_GET_CFG, cfg);
    mpp_dec_cfg_set_u32(cfg, "base:split_parse", 1);
    mpi->control(ctx, MPP_DEC_SET_CFG, cfg);
    mpp_dec_cfg_deinit(cfg);

    /* ========== NV12 输出 buffer 池（零拷贝到编码器） ========== */
    RK_U32 hor_stride = MPP_ALIGN(WIDTH, 64);   /* 1280 */
    RK_U32 ver_stride = MPP_ALIGN(HEIGHT, 64);     /* 768  */
    size_t nv12_size  = hor_stride * ver_stride * 2;   /* ×4: MPP 硬件写帧可能溢出逻辑尺寸 */

    int ion_fd = open("/dev/ion", O_RDWR);
    if (ion_fd < 0) {
        perror("open /dev/ion");
        mpp_destroy(ctx);
        frame_queue_stop(q->input_q);
        return NULL;
    }

    DmaBuffer nv12_pool[NV12_BUF_NUM] = {0};
    for (int i = 0; i < NV12_BUF_NUM; i++) {
        nv12_pool[i].mxlen = nv12_size;
        if (dma_ion_alloc(ion_fd, nv12_size, &nv12_pool[i]) < 0) {
            dma_bufs_release(nv12_pool, i);
            close(ion_fd);
            mpp_destroy(ctx);
            frame_queue_stop(q->input_q);
            return NULL;
        }
    }
    close(ion_fd);

    /* 内部空闲队列 */
    for (int i = 0; i < NV12_BUF_NUM; i++)
        queue_push(q->free_nv12, &nv12_pool[i]);


    /* 单个 MppFrame 描述符 — buffer 指针每帧切换，其他属性设一次 */
    MppFrame frame_out = NULL;
    ret = mpp_frame_init(&frame_out);
    if (ret) {
        fprintf(stderr, "mpp_frame_init failed %d\n", ret);
        dma_bufs_release(nv12_pool, NV12_BUF_NUM);
        frame_queue_destroy(q->free_nv12);
        mpp_destroy(ctx);
        frame_queue_stop(q->input_q);
        return NULL;
    }
    mpp_frame_set_width(frame_out, 1280);
    mpp_frame_set_height(frame_out, 720);
    mpp_frame_set_hor_stride(frame_out, hor_stride);
    mpp_frame_set_ver_stride(frame_out, ver_stride);
    mpp_frame_set_fmt(frame_out, MPP_FMT_YUV420SP);


    while (g_running) {
        DmaBuffer pkt;
        if (queue_pop(q->input_q, &pkt) < 0)
            break;

        /* ---- 导入 V4L2 的 MJPEG dmabuf 作为 MPP 输入 ---- */
        MppBufferInfo info = {
            .type = MPP_BUFFER_TYPE_ION,
            .fd   = pkt.dmabuf,
            .size = pkt.mxlen,
        };
        MppBuffer buf = NULL;
        ret = mpp_buffer_import(&buf, &info);
        if (ret != MPP_OK) {
            fprintf(stderr, "mpp_buffer_import %d\n", ret);
            qbuf_to_v4l2(q, pkt);
            continue;
        }

        MppPacket packet = NULL;
        ret = mpp_packet_init_with_buffer(&packet, buf);
        if (ret != MPP_OK) {
            fprintf(stderr, "mpp_packet_init_with_buffer %d\n", ret);
            mpp_buffer_put(buf);
            qbuf_to_v4l2(q, pkt);
            continue;
        }
        mpp_packet_set_length(packet, pkt.reallen);

        /* ---- 从空闲池取一块 NV12 buffer 作为解码输出 ---- */
        DmaBuffer nv12_out;
        if (queue_pop(q->free_nv12, &nv12_out) < 0)
            break;

        /* dup() 后 import，确保 nv12_out.dmabuf fd 不被 MPP 关掉 */
        int dup_fd = dup(nv12_out.dmabuf);
        MppBufferInfo out_info = {
            .type = MPP_BUFFER_TYPE_ION,
            .fd   = dup_fd,
            .size = nv12_out.mxlen,
        };
        MppBuffer out_buf = NULL;
        ret = mpp_buffer_import(&out_buf, &out_info);
        if (ret != MPP_OK) {
            close(dup_fd);
            fprintf(stderr, "mpp_buffer_import out %d\n", ret);
            queue_push(q->free_nv12, &nv12_out);
            goto cleanup_packet;
        }
        mpp_frame_set_buffer(frame_out, out_buf);

        /* ---- MppTask 解码 ---- */
        MppTask task = NULL;

        ret = mpi->poll(ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
        if (ret) {
            fprintf(stderr, "mpp input poll failed %d\n", ret);
            mpp_buffer_put(out_buf);
            queue_push(q->free_nv12, &nv12_out);
            goto cleanup_packet;
        }

        ret = mpi->dequeue(ctx, MPP_PORT_INPUT, &task);
        if (ret || !task) {
            fprintf(stderr, "mpp input dequeue failed %d\n", ret);
            mpp_buffer_put(out_buf);
            queue_push(q->free_nv12, &nv12_out);
            goto cleanup_packet;
        }

        mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);
        mpp_task_meta_set_frame (task, KEY_OUTPUT_FRAME,  frame_out);

        ret = mpi->enqueue(ctx, MPP_PORT_INPUT, task);
        if (ret) {
            fprintf(stderr, "mpp input enqueue failed %d\n", ret);
            mpp_buffer_put(out_buf);
            queue_push(q->free_nv12, &nv12_out);
            goto cleanup_packet;
        }

        ret = mpi->poll(ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
        if (ret) {
            fprintf(stderr, "mpp output poll failed %d\n", ret);
            mpp_buffer_put(out_buf);
            queue_push(q->free_nv12, &nv12_out);
            goto cleanup_packet;
        }

        ret = mpi->dequeue(ctx, MPP_PORT_OUTPUT, &task);
        if (ret || !task) {
            fprintf(stderr, "mpp output dequeue failed %d\n", ret);
            mpp_buffer_put(out_buf);
            queue_push(q->free_nv12, &nv12_out);
            goto cleanup_packet;
        }


        mpi->enqueue(ctx, MPP_PORT_OUTPUT, task);

        /* ---- 清理 MJPEG 侧 MPP 资源 ---- */
        mpp_packet_deinit(&packet);
        mpp_buffer_put(buf);       /* 释放导入的 MJPEG buffer */
        mpp_buffer_put(out_buf);   /* 释放 dup 的 fd；nv12_out.dmabuf 保持有效 */
        qbuf_to_v4l2(q, pkt);

        /* ---- 把解码后的 NV12 dmabuf 推给编码器 ---- */
        /*
         * 零拷贝交接：
         *   编码器从 q->out_q pop，编码完成后把 dmabuf fd 推回 free_nv12。
         *   当前还没编码器，先自回收保持循环。
         */
        if (!nv12_out.start) {
            nv12_out.mxlen = nv12_size;
            buf_mmap(&nv12_out);
        }
        queue_push(q->out_q, &nv12_out);
        continue;

cleanup_packet:
        mpp_packet_deinit(&packet);
        mpp_buffer_put(buf);
        qbuf_to_v4l2(q, pkt);
    }

    mpp_frame_deinit(frame_out);
    dma_bufs_release(nv12_pool, NV12_BUF_NUM);
    frame_queue_stop(q->free_nv12);
    frame_queue_stop(q->out_q);
    mpp_destroy(ctx);
    return NULL;
}
void *thread_mpp_enc(void *argv)
{
    AQueue *q = (AQueue*)argv;
    MppCtx ctx = NULL; 
    MppApi *mpi = NULL;
    MPP_RET ret;

    ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) {
        fprintf(stderr, "[ERR] mpp_create enc fail %d\n", ret);
        return NULL;
    }

    ret = mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) {
        fprintf(stderr, "[ERR] mpp_init enc avc fail %d\n", ret);
        mpp_destroy(ctx);
        return NULL;
    }
    MppEncCfg cfg = NULL;
    mpp_enc_cfg_init(&cfg);

    // 获取默认配置
    mpi->control(ctx, MPP_ENC_GET_CFG, cfg);

    // 基本参数：宽高、步幅、格式
    mpp_enc_cfg_set_s32(cfg, "prep:width", WIDTH);
    mpp_enc_cfg_set_s32(cfg, "prep:height", HEIGHT);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", MPP_ALIGN(WIDTH, 64));
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", MPP_ALIGN(HEIGHT, 64));
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);

    // 编码器码流类型
    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:bps", 2 * 1024 * 1024); // 2Mbps

    // 设置 GOP 等（一般 30 帧一个 I 帧）
    mpp_enc_cfg_set_s32(cfg, "split:mode", MPP_ENC_SPLIT_NONE);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", 30);


    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", 30);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", 1);

    ret = mpi->control(ctx, MPP_ENC_SET_CFG, cfg);
    if (ret != MPP_OK) {
        fprintf(stderr, "MPP_ENC_SET_CFG failed\n");
        mpp_enc_cfg_deinit(cfg);
        mpp_destroy(ctx);
        return NULL;
    }
    mpp_enc_cfg_deinit(cfg);
    FILE *out_fp = fopen("output.h264", "wb");
    if (!out_fp) {
        fprintf(stderr, "fopen output.h264 failed\n");
        mpp_destroy(ctx);
        return NULL;
    }
    RK_U32 hor_stride = MPP_ALIGN(WIDTH, 16);   /* 1280 */
    RK_U32 ver_stride = MPP_ALIGN(HEIGHT, 16);
    size_t frame_size = MPP_ALIGN(hor_stride, 64) * MPP_ALIGN(ver_stride, 64) * 3 / 2;
    MppBufferGroup buf_grp = NULL;
    mpp_buffer_group_get_internal(buf_grp, MPP_BUFFER_TYPE_DRM);
    MppBuffer buf = NULL;
    mpp_buffer_get(buf_grp, &buf, frame_size);
    
    /* === 生成 H.264 头（SPS/PPS）=== */
    {
        MppPacket hdr = NULL;
        mpp_packet_init_with_buffer(&hdr, buf);
        mpp_packet_set_length(hdr, 0);
        ret = mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, hdr);
        if (ret != MPP_OK || !hdr) {
            fprintf(stderr, "[WARN] MPP_ENC_GET_HDR_SYNC ret=%d, hdr=%p\n", ret, hdr);
        } else {
            void *data = mpp_packet_get_data(hdr);
            size_t len = mpp_packet_get_length(hdr);
            if (data && len)
                fwrite(data, 1, len, out_fp);
            mpp_packet_deinit(&hdr);
            mpp_buffer_put(buf);
            buf = NULL;
            mpp_buffer_group_put(buf_grp);
            buf_grp = NULL;
        }
    }
    MppFrame frame_out = NULL;
    ret = mpp_frame_init(&frame_out);
    if (ret) {
        fprintf(stderr, "[ERR] mpp_frame_init failed %d\n", ret);
        fclose(out_fp);
        mpp_destroy(ctx);
        return NULL;
    }
    mpp_frame_set_width(frame_out, WIDTH);
    mpp_frame_set_height(frame_out, HEIGHT);
    mpp_frame_set_hor_stride(frame_out, MPP_ALIGN(WIDTH, 64));
    mpp_frame_set_ver_stride(frame_out, MPP_ALIGN(HEIGHT, 64));
    mpp_frame_set_fmt(frame_out, MPP_FMT_YUV420SP);
    
    

    while (g_running)
    {
        DmaBuffer nv12_in;
        if (queue_pop(q->out_q, &nv12_in) < 0)
            break;

        /* dup() 后 import，确保 nv12_in.dmabuf fd 不被 MPP 关掉 */
        int dup_fd = dup(nv12_in.dmabuf);
        MppBufferInfo in_info = {
            .type = MPP_BUFFER_TYPE_ION,
            .fd   = dup_fd,
            .size = nv12_in.mxlen,
        };
        MppBuffer in_buf = NULL;
        ret = mpp_buffer_import(&in_buf, &in_info);
        if (ret != MPP_OK) {
            close(dup_fd);
            fprintf(stderr, "mpp_buffer_import in %d\n", ret);
            queue_push(q->free_nv12, &nv12_in);
            continue;
        }
        

        mpp_frame_set_buffer(frame_out, in_buf);


        ret=mpi->encode_put_frame(ctx, frame_out);
        if (ret!=MPP_OK) {
            fprintf(stderr, "encode_put_frame failed %d\n", ret); 
            mpp_buffer_put(in_buf);
            queue_push(q->free_nv12, &nv12_in);
            continue;   
        }
        MppPacket packet = NULL;
        while(1)
        {
            ret=mpi->encode_get_packet(ctx, &packet);
            if (ret == MPP_OK && packet) {
                void *data = mpp_packet_get_data(packet);
                size_t len = mpp_packet_get_length(packet);
                if(data)
                    fwrite(data, 1, len, out_fp);
                mpp_packet_deinit(&packet);
            } 
            else
            {
                break;
            }
        }
        mpp_buffer_put(in_buf);
        queue_push(q->free_nv12, &nv12_in);

    }
    fclose(out_fp);
    mpp_frame_deinit(frame_out);
    mpp_destroy(ctx);
    return NULL;
}
