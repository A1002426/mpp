#ifndef	_M_QUEUE_H
#define _M_QUEUE_H
#include<head.h>
#include<pthread.h>
#include<stdlib.h>
#include "m_mpp.h"
typedef struct  {
    DmaBuffer *buffer;   // 环形缓冲区
    int capacity;
    int front;
    int rear;
    int count;
    int stop;              // 停止标志，用于唤醒阻塞线程
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
}Queue;
typedef struct 
{
    Queue *input_q;
    Queue *done_q;
    Queue *out_q;
    Queue *free_nv12;
    int fd;
}AQueue;
Queue *quenue_create(int mxsize);
int queue_push(Queue *q, const DmaBuffer *pkt);
int queue_pop(Queue *q, DmaBuffer *pkt);
void frame_queue_destroy(Queue *q);


#endif