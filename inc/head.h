#ifndef	_HEAD_H
#define _HEAD_H
#include <linux/dma-buf.h>
#include <ion.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
typedef struct 
{
    int dmabuf;
    void *start;
    size_t mxlen;
    size_t reallen;
    int  buf_index;
}DmaBuffer;


int dma_ion_alloc(int ion_fd,size_t size, DmaBuffer *out);
int buf_mmap(DmaBuffer *out);
void dma_buf_free(DmaBuffer *buf);
void dma_bufs_release(DmaBuffer *bufs,int len);


#endif