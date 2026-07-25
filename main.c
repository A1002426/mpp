#include <signal.h>
#include<v4l2.h>
#include"m_thread.h"
volatile int g_running = 1;
void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}




int main(int argc, char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    pthread_t pid_v4l2;
    pthread_t pid_mpp_dec;
    //pthread_t pid_v4l2dq;
    AQueue a;
    memset(&a, 0, sizeof(AQueue));
    a.input_q=quenue_create(BUF_NUM);
    a.done_q=quenue_create(BUF_NUM);
    a.free_nv12 = quenue_create(NV12_BUF_NUM);
    a.out_q = quenue_create(NV12_BUF_NUM);
    pthread_create(&pid_v4l2,NULL,thread_v4l2,&a);
    //pthread_create(&pid_v4l2dq,NULL,thread_v4l2dq,&a);
    pthread_create(&pid_mpp_dec,NULL,thread_mpp_dec,&a);
    //pthread_join(pid_v4l2dq,NULL);
    pthread_join(pid_v4l2,NULL);
    pthread_join(pid_mpp_dec,NULL);
    return 0;
}
