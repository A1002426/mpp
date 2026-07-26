#ifndef	_V4L2_H
#define _V4L2_H
#include<linux/videodev2.h>
#include<head.h>
#include"m_queue.h"
extern volatile int g_running;
#define BUF_NUM 4
#define NV12_BUF_NUM    4
#define HEIGHT 720
#define WIDTH 1280
#define DENOMINATOR 30
typedef struct 
{
    unsigned int pixelformat;
    char description[32];
}capfmt;
typedef struct 
{
    int width;
    int height;
}fs;
typedef struct
{
    int numerator;
    int denominator;
}fi;
int capability(int fd);
int select_v4l2(int fd,capfmt *fmt_li, fs *fs_li, fi *fi_li);
int set_fmt(int fd,capfmt *fmt_li, fs *fs_li);
int set_streamparm(int fd, fi *fi_li);
int qbuf(int fd, DmaBuffer *dma_bufs);
int dqbuf(int fd, DmaBuffer *dma_bufs, FILE *fp);



#endif