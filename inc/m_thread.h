#ifndef	_M_THREAD_H
#define _M_THREAD_H
#include "v4l2.h"
#include "m_mpp.h"
#include "m_queue.h"
#include <stdio.h>

void *thread_v4l2(void *argv);
void *thread_mpp(void *argv);
void* thread_v4l2dq(void* argv);

#endif