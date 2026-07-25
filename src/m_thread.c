#include"m_thread.h"
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
        return NULL;
    }
    if(!capability(fd))
    {
        close(fd);
        return NULL;
    }
    capfmt fmt_li={0};
    fs fs_li={0};
    fmt_li.pixelformat=V4L2_PIX_FMT_MJPEG;
    fs_li.height=720;
    fs_li.width=1280;
    if(!set_fmt(fd, &fmt_li, &fs_li))
    {
        close(fd);
        return NULL;
    }
    if (!qbuf(fd, dma_bufs))
    {
        close(fd);
        return NULL;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd, VIDIOC_STREAMON, &type) < 0)
    {
        perror("VIDIOC_STREAMON");  
        close(fd);
        return NULL;
    }
    FILE *fp = fopen("frame.mjpg", "wb");
    if(!fp)    {
        perror("fopen");
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

void *thread_mpp(void *argv)
{
    AQueue *q= (AQueue*)argv;
    MppCtx ctx=NULL; 
    MppApi *mpi=NULL;
    MPP_RET ret;

    ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) {
        fprintf(stderr, "[ERR] mpp_create dec fail %d\n", ret);
        return NULL;
    }

    ret = mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        fprintf(stderr, "[ERR] mpp_init dec mjpeg fail %d\n", ret);
        mpp_destroy(ctx);
        return NULL;
    }

    /* 设置 split_parse，MJPEG 需要用 1 */
    MppDecCfg cfg = NULL;
    mpp_dec_cfg_init(&cfg);
    mpi->control(ctx, MPP_DEC_GET_CFG, cfg);
    mpp_dec_cfg_set_u32(cfg, "base:split_parse", 1);
    mpi->control(ctx, MPP_DEC_SET_CFG, cfg);
    mpp_dec_cfg_deinit(cfg);

    /* 创建 buffer group 和输出 frame（MJPEG 必须用 MppTask API） */
    MppBufferGroup frm_grp = NULL;
    ret = mpp_buffer_group_get_internal(&frm_grp, MPP_BUFFER_TYPE_ION);
    if (ret) {
        fprintf(stderr, "mpp_buffer_group_get_internal failed %d\n", ret);
        mpp_destroy(ctx);
        return NULL;
    }

    /* 1280x720 MJPEG 解码输出 NV12，申请足够大的 buffer */
    MppFrame frame_out = NULL;
    ret = mpp_frame_init(&frame_out);
    if (ret) {
        fprintf(stderr, "mpp_frame_init failed %d\n", ret);
        mpp_buffer_group_put(frm_grp);
        mpp_destroy(ctx);
        return NULL;
    }

    RK_U32 hor_stride = 1280;  /* MPP_ALIGN(width, 16) = 1280 */
    RK_U32 ver_stride = 720;   /* MPP_ALIGN(height, 16) = 720 */
    MppBuffer frm_buf = NULL;
    ret = mpp_buffer_get(frm_grp, &frm_buf, hor_stride * ver_stride * 4);
    if (ret) {
        fprintf(stderr, "mpp_buffer_get failed %d\n", ret);
        mpp_frame_deinit(frame_out);
        mpp_buffer_group_put(frm_grp);
        mpp_destroy(ctx);
        return NULL;
    }
    mpp_frame_set_buffer(frame_out, frm_buf);

    FILE *fp = fopen("frame.nv12", "wb");
    if(!fp) {
        perror("fopen");
        mpp_buffer_put(frm_buf);
        mpp_frame_deinit(frame_out);
        mpp_buffer_group_put(frm_grp);
        mpp_destroy(ctx);
        return NULL;
    }

    while(g_running)
    {
        DmaBuffer pkt;
        if(queue_pop(q->input_q, &pkt) < 0)
            break;

        /* 导入 V4L2 的 dmabuf 作为 MPP 输入 buffer */
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
        
        /* 创建 packet */
        MppPacket packet = NULL;
        ret = mpp_packet_init_with_buffer(&packet, buf);
        if (ret != MPP_OK) {
            fprintf(stderr, "mpp_packet_init_with_buffer %d\n", ret);
            mpp_buffer_put(buf);
            qbuf_to_v4l2(q, pkt);
            continue;
        }
        mpp_packet_set_length(packet, pkt.reallen);

        /* === MppTask 方式送帧 === */
        MppTask task = NULL;

        /* 等待 input 端口可用 */
        ret = mpi->poll(ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
        if (ret) {
            fprintf(stderr, "mpp input poll failed %d\n", ret);
            goto cleanup_packet;
        }

        /* 从 input 队列取出一个 task */
        ret = mpi->dequeue(ctx, MPP_PORT_INPUT, &task);
        if (ret || !task) {
            fprintf(stderr, "mpp input dequeue failed %d\n", ret);
            goto cleanup_packet;
        }

        /* 设置 task 的输入 packet 和输出 frame */
        mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);
        mpp_task_meta_set_frame (task, KEY_OUTPUT_FRAME,  frame_out);

        /* 提交 task */
        ret = mpi->enqueue(ctx, MPP_PORT_INPUT, task);
        if (ret) {
            fprintf(stderr, "mpp input enqueue failed %d\n", ret);
            goto cleanup_packet;
        }

        /* 等待 output 端口 */
        ret = mpi->poll(ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
        if (ret) {
            fprintf(stderr, "mpp output poll failed %d\n", ret);
            goto cleanup_packet;
        }

        /* 从 output 队列取出完成后的 task */
        ret = mpi->dequeue(ctx, MPP_PORT_OUTPUT, &task);
        if (ret || !task) {
            fprintf(stderr, "mpp output dequeue failed %d\n", ret);
            goto cleanup_packet;
        }

        /* 获取解码后的 frame 并写入文件 */
        {
            MppFrame frame_ret = NULL;
            mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &frame_ret);
            if (frame_ret) {
                MppBuffer buf_out = mpp_frame_get_buffer(frame_ret);
                if (buf_out) {
                    void *ptr = mpp_buffer_get_ptr(buf_out);
                    size_t size = mpp_buffer_get_size(buf_out);
                    fwrite(ptr, 1, size, fp);
                }
            }
        }

        /* 归还 output task */
        mpi->enqueue(ctx, MPP_PORT_OUTPUT, task);

        /* 回收 input task 和 packet */
        {
            MppTask in_task = NULL;
            mpi->dequeue(ctx, MPP_PORT_INPUT, &in_task);
            if (in_task) {
                MppPacket pkt_ret = NULL;
                mpp_task_meta_get_packet(in_task, KEY_INPUT_PACKET, &pkt_ret);
                if (pkt_ret)
                    mpp_packet_deinit(&pkt_ret);
                mpi->enqueue(ctx, MPP_PORT_INPUT, in_task);
            }
        }

        mpp_buffer_put(buf);
        qbuf_to_v4l2(q, pkt);
        continue;

cleanup_packet:
        mpp_packet_deinit(&packet);
        mpp_buffer_put(buf);
        qbuf_to_v4l2(q, pkt);
    }

    fclose(fp);
    mpp_buffer_put(frm_buf);
    mpp_frame_deinit(frame_out);
    mpp_buffer_group_put(frm_grp);
    mpp_destroy(ctx);
    frame_queue_destroy(q->input_q);
    return NULL;
}
