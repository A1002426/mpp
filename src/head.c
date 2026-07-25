#include<head.h>

int dma_ion_alloc(int ion_fd,size_t size, DmaBuffer *out)
{
    if (!out || size == 0)
        return -1;

   

    // ION 专属分配结构体，替换dma_heap结构体
    struct ion_allocation_data alloc = {
        .len = size,
        .heap_id_mask =(1<<2), // 使用DMA堆
        .flags = 0,
    };

    // ION 原生ioctl命令
    if (ioctl(ion_fd, ION_IOC_ALLOC, &alloc) < 0) {
        perror("ION_IOC_ALLOC");
        close(ion_fd);
        return -1;
    }
    
    out->dmabuf = alloc.fd;
    out->mxlen=0;
    out->reallen=0;
    out->start=NULL;
    return 0;
}
int buf_mmap(DmaBuffer *out)    
{
    out->start = mmap(NULL, out->mxlen, PROT_READ | PROT_WRITE, MAP_SHARED, out->dmabuf, 0);
    if (out->start == MAP_FAILED) {
        out->start=NULL;
        perror("dmabuf mmap");
        close(out->dmabuf);
        out->dmabuf = -1;
        return -1;
    }
    
    return 0;
}
void dma_buf_free(DmaBuffer *buf)
{
    if (!buf)
        return;
    if (buf->start) {
        munmap(buf->start, buf->mxlen);
        buf->start = NULL;
    }
    if (buf->dmabuf >= 0) {
        close(buf->dmabuf);
        buf->dmabuf = -1;
    }
    buf->mxlen = 0;
}
void dma_bufs_release(DmaBuffer *bufs,int len)
{
    if (!bufs)
        return;
    for (int i = 0; i < len; i++) {
        dma_buf_free(&bufs[i]);
    }
}