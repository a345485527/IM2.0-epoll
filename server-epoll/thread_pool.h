#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include "./protocol.h"

/*
 * work queue
 * thread fetch work in this queue
 */
typedef struct worker{
    // call-back function, when task is running,it will be call this function
    void (*process)(p_base*,int);
    p_base* ptr;
    int sockfd;
    struct worker* next;
}CThread_worker;

/*
 * thread pool
 */
typedef struct{
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_ready;

    // the max thread number;
    int max_thread_num;
    // destory the thread pool
    bool isDestroy;
   
    // the array of every thread id
    pthread_t *thread_id;
   
    // work queue
    CThread_worker* queue_head;
    int queue_size;
}CThread_pool;

/*
 * init the thread pool
 * parm: the max number of thread
 */
void pool_init(int);

/*
 * add new work to work queue
 * parm: the call-back function
 *       the pointer to p_base
 *       the socket
 */
void pool_add_work(void(*p)(p_base*,int),p_base*,int);

/*
 * destory the thread pool_destroy
 * no parm
 */
void pool_destroy();

void * thread_routine(void*);
extern CThread_pool *pool;
#endif
