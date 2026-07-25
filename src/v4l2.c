#include<v4l2.h>

int capability(int fd)
{
    struct v4l2_capability cap={0};
    if(ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        perror("VIDIOC_QUERYCAP");
        return 0;
    }
    if(!(cap.capabilities&V4L2_CAP_VIDEO_CAPTURE))
    {
        fprintf(stderr, "Not a capture device\n");
        return 0;
    }
    return 1;
}
int select_v4l2(int fd,capfmt *fmt_li, fs *fs_li, fi *fi_li)
{
    struct v4l2_fmtdesc fmtdesc={0};//像素格式描述
    capfmt fmt_list[20];
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0)
    {
        fmt_list[fmtdesc.index].pixelformat = fmtdesc.pixelformat;
        strncpy(fmt_list[fmtdesc.index].description, (const char *)fmtdesc.description, 32);
        fmt_list[fmtdesc.index].description[31] = '\0';
        printf("Format %d: 0x%08x (%s)\n", fmtdesc.index, fmtdesc.pixelformat, fmtdesc.description);
        fmtdesc.index++;
    }
    int sel_fmt;
    printf("choose format: ");
    scanf("%d", &sel_fmt);
    if(sel_fmt < 0 || sel_fmt >= fmtdesc.index)
    {
        fprintf(stderr, "Invalid format selection\n");
        return 0;
    }
    fmt_li->pixelformat = fmt_list[sel_fmt].pixelformat;
    strncpy(fmt_li->description, fmt_list[sel_fmt].description, 31);


    struct v4l2_frmsizeenum frmsize={0};// 分辨率尺寸枚举
    frmsize.index = 0;
    frmsize.pixel_format = fmt_list[sel_fmt].pixelformat;
    fs fs_list[20];
    while(ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0)
    {        
        if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
        {
            printf("  Size %d: %dx%d\n", frmsize.index, frmsize.discrete.width, frmsize.discrete.height);
            fs_list[frmsize.index].width = frmsize.discrete.width;
            fs_list[frmsize.index].height = frmsize.discrete.height;
        }
        frmsize.index++;
    }
    int sel_fs;
    printf("choose frame size: ");
    scanf("%d", &sel_fs);
    if(sel_fs < 0 || sel_fs >= frmsize.index)
    {
        fprintf(stderr, "Invalid frame size selection\n");
        return 0;
    }
    fs_li->width = fs_list[sel_fs].width;
    fs_li->height = fs_list[sel_fs].height;


    struct v4l2_frmivalenum frmival={0};//帧率间隔枚举
    fi fi_list[20];
    frmival.index = 0;
    frmival.pixel_format = fmt_list[sel_fmt].pixelformat;
    frmival.width = fs_list[sel_fs].width;
    frmival.height = fs_list[sel_fs].height;
    while (ioctl(fd,VIDIOC_ENUM_FRAMEINTERVALS,&frmival) == 0)
    {
        if(frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE)
        {
            printf("Interval %d: %d/%d seconds\n", frmival.index, frmival.discrete.numerator, frmival.discrete.denominator);
            fi_list[frmival.index].numerator = frmival.discrete.numerator;
            fi_list[frmival.index].denominator = frmival.discrete.denominator;
        }
        frmival.index++;
    }
    int sel_fi;
    printf("choose frame interval: ");
    scanf("%d", &sel_fi);
    if(sel_fi < 0 || sel_fi >= frmival.index)
    {
        fprintf(stderr, "Invalid frame interval selection\n");
        return 0;
    }
    fi_li->numerator = fi_list[sel_fi].numerator;
    fi_li->denominator = fi_list[sel_fi].denominator;
    return 1;
}
int set_fmt(int fd,capfmt *fmt_li, fs *fs_li)
{
    struct v4l2_format fmt={0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = fs_li->width;
    fmt.fmt.pix.height = fs_li->height;
    fmt.fmt.pix.pixelformat = fmt_li->pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if(ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        perror("VIDIOC_S_FMT");
        return 0;
    }
    fs_li->width = fmt.fmt.pix.width;
    fs_li->height = fmt.fmt.pix.height;
    fmt_li->pixelformat = fmt.fmt.pix.pixelformat;
    printf("Capture: %dx%d, format=0x%08x\n", fs_li->width, fs_li->height, fmt_li->pixelformat);
    return 1;
}
int set_streamparm(int fd, fi *fi_li)
{
    struct v4l2_streamparm streamparm={0};
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd, VIDIOC_G_PARM, &streamparm) < 0)
    {
        perror("VIDIOC_G_PARM");
        return 0;
    }
    if(streamparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)
    {
        streamparm.parm.capture.timeperframe.numerator = fi_li->numerator;
        streamparm.parm.capture.timeperframe.denominator = fi_li->denominator;
        if(ioctl(fd, VIDIOC_S_PARM, &streamparm) < 0)
        {
            perror("VIDIOC_S_PARM");
            return 0;
        }
    }
    else
    {
        fprintf(stderr, "Device does not support setting frame interval\n");
        return 0;
    }
    return 1;
}
int qbuf(int fd, DmaBuffer *dma_bufs)
{
    struct v4l2_requestbuffers req={0};
    req.count = BUF_NUM;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_DMABUF;
    if(ioctl(fd, VIDIOC_REQBUFS, &req) < 0 )
    {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return 0;
    }
    // 获取一帧图像大小
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0)
    {
        perror("VIDIOC_G_FMT get format failed");
        return 0;
    }
    size_t frame_size = fmt.fmt.pix.sizeimage;
    printf("frame size = %zu\n", frame_size);
     // ION 标准设备节点
    int ion_fd = open("/dev/ion", O_RDWR);
    if (ion_fd < 0) {
        perror("open /dev/ion");
        return -1;
    }
    // 循环分配DMA内存，把dmabuf fd传给V4L
    for (int i = 0; i < BUF_NUM; i++)
    {
        if (dma_ion_alloc(ion_fd, frame_size, &dma_bufs[i]) < 0)
        {
            dma_bufs_release(dma_bufs,BUF_NUM);
            close(fd);
            return 0;
        }
        struct v4l2_buffer qbuf = {0};
        qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        qbuf.memory = V4L2_MEMORY_DMABUF;
        qbuf.index = i;
        qbuf.m.fd = dma_bufs[i].dmabuf; // 核心：传入自己分配的dmabuf fd
        if (ioctl(fd, VIDIOC_QBUF, &qbuf) < 0)
        {
            perror("QBUF dmabuf");
            dma_bufs_release(dma_bufs,BUF_NUM);
            close(fd);
            return 0;
        }
    }
    close(ion_fd);
    return 1;
}
int dqbuf(int fd, DmaBuffer *dma_bufs, FILE *fp)
{
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_DMABUF;
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0)
    {
        perror("VIDIOC_DQBUF");
        return 0;
    }
    dma_bufs[buf.index].reallen=buf.bytesused;
    dma_bufs[buf.index].mxlen=buf.length;
    if(!dma_bufs[buf.index].start)
    {
        buf_mmap(&dma_bufs[buf.index]);
    }
    fwrite(dma_bufs[buf.index].start, 1,buf.bytesused, fp);
    
    //ioctl(fd, VIDIOC_QBUF, &buf);
    return 1;
}

