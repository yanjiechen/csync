#include <stdio.h>
#include <pthread.h>
#include <signal.h>

pthread_t t1;
pthread_t t2;

void sig_term(int signo)
{
    printf("thread(%u) received SIGTERM", pthread_self());
    signal(SIGTERM, sig_term);
}

void *thread1(void *arg)
{
    printf("thread1(%u): started\n", pthread_self());
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    printf("thread1: disable cancel\n");
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    sleep(3);
    printf("thread1: here we go\n");
    printf("thread1: enable cancel\n");
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    printf("thread1: test cancel\n");
    pthread_testcancel();
    printf("thread1: return\n");
}

void *thread2(void *arg)
{
    printf("thread2(%u): started\n\n", pthread_self());
    sleep(1);
    printf("thread2: cancel thread1\n");
    pthread_cancel(t1);
    printf("thread2: thread canceled\n");
    printf("thread2: return\n");
}

main(int argc, char **argv)
{
    pthread_create(&t1, NULL, thread1, NULL);
    pthread_create(&t2, NULL, thread2, NULL);
    pthread_join(t2, NULL);
    printf("thread2: exited\n\n");
    pthread_join(t1, NULL);
    printf("thread1: exited\n");
}