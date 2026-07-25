#include"m_queue.h"
Queue *quenue_create(int mxsize)
{
    Queue *q = (Queue*)calloc(1, sizeof(Queue));
    if (!q) return NULL;
    q->capacity = mxsize;
    q->buffer = (DmaBuffer*)calloc(mxsize, sizeof(DmaBuffer));
    if (!q->buffer) {
        free(q);
        return NULL;
    }
    q->front = q->rear = q->count = 0;
    q->stop = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return q;
}

int queue_push(Queue *q, const DmaBuffer *pkt) 
{
    if (!q) return -1;
    pthread_mutex_lock(&q->mutex);
    // 队列满且未停止时等待
    while (q->count == q->capacity && !q->stop) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    if (q->stop) {
        pthread_mutex_unlock(&q->mutex);
        return -1;          // 队列已停止，推入失败
    }
    // 拷贝数据到队尾
    q->buffer[q->rear] = *pkt;
    q->rear = (q->rear + 1) % q->capacity;
    q->count++;
    // 通知非空
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

int queue_pop(Queue *q, DmaBuffer *pkt) 
{
    if (!q || !pkt) return -1;
    pthread_mutex_lock(&q->mutex);
    // 队列空且未停止时等待
    while (q->count == 0 && !q->stop) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    if (q->count == 0 && q->stop) {
        pthread_mutex_unlock(&q->mutex);
        return -1;          // 队列已停止且无数据，弹出失败
    }
    // 从队首取出
    *pkt = q->buffer[q->front];
    q->front = (q->front + 1) % q->capacity;
    q->count--;
    // 通知非满
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

void frame_queue_stop(Queue *q) {
    if (!q) return;
    pthread_mutex_lock(&q->mutex);
    q->stop = 1;
    // 唤醒所有等待的线程（推入或弹出都会因 stop 而退出）
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

void frame_queue_destroy(Queue *q) 
{
    if (!q) return;
    // 先停止队列，唤醒所有等待线程
    frame_queue_stop(q);
    // 等待所有持有锁的线程释放后再销毁（这里简单处理，调用者需保证线程已退出）
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    free(q->buffer);
    free(q);
}