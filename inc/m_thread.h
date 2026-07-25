#ifndef	_M_THREAD_H
#define _M_THREAD_H
#include "v4l2.h"
#include "m_mpp.h"
#include "m_queue.h"
#include <stdio.h>
#define NV12_BUF_NUM    4

void *thread_v4l2(void *argv);
void *thread_mpp_dec(void *argv);

#endif