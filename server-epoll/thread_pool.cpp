#include "./thread_pool.h"
#include <iostream>
#include <cstdlib>
using std::cout;

void pool_init(int _max_thread_num)
{
    pool=(CThread_pool*)malloc(sizeof(CThread_pool));
    pthread_mutex_init(&(pool->queue_mutex), NULL);
    pthread_cond_init(&(pool->queue_ready), NULL);
    pool->max_thread_num=_max_thread_num;
    pool->isDestroy=false;
    pool->thread_id=(pthread_t*)malloc(sizeof(pthread_t)*_max_thread_num);
    pool->queue_head=NULL;
    pool->queue_size=0;
    // create threads
    for(int i=0;i<_max_thread_num;++i)
    {
        pthread_create(&(pool->thread_id[i]), NULL, thread_routine, NULL);
    }
}

void pool_add_work(void(*p)(p_base*,int),p_base* _ptr,int _sockfd)
{
    cout<<"add work\n";
    CThread_worker *work=(CThread_worker*)malloc(sizeof(CThread_worker));
    work->process=p;
    work->ptr=_ptr;
    work->sockfd=_sockfd;
    work->next=NULL;
    pthread_mutex_lock(&pool->queue_mutex);
    if(NULL==pool->queue_head)
    {
        pool->queue_head=work;
    }
    else
    {
       CThread_worker* tmp=pool->queue_head;
       while(tmp->next)
       {
            tmp=tmp->next;
       }
       tmp->next=work;
    }
    pool->queue_size++;
    pthread_mutex_unlock(&pool->queue_mutex);
    int n=pthread_cond_signal(&pool->queue_ready);
}

void pool_destroy()
{
    // wakeup all thread,thread pool will be destroyed
    pool->isDestroy=true;
    pthread_cond_broadcast(&pool->queue_ready);

    // wait for all threads exit
    for(int i=0;i<pool->max_thread_num;i++)
    {
        pthread_join(pool->thread_id[i], NULL);
    }
    
    // free the mutex
    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_cond_destroy(&pool->queue_ready);
    // free the thread_id array
    free(pool->thread_id);
    pool->thread_id=NULL;
    //free the work queue
    while(pool->queue_head!=NULL)
    {
        CThread_worker* tmp=pool->queue_head->next;
        free(pool->queue_head);
        pool->queue_head=tmp;
    }
    pool->queue_head=NULL;
    // free thread pool
    free(pool);
    pool=NULL;
}


void *thread_routine(void * arg)
{
    cout<<"create thread pool thread\n";
    while(1)
    {
        pthread_mutex_lock(&pool->queue_mutex);    
        while(pool->queue_head==NULL&&pool->isDestroy!=true)
        {
            cout<<"queue is NULL\n";
            pthread_cond_wait(&pool->queue_ready, &pool->queue_mutex);
        }

        // if thread pool will be destroyed
        // the condition is true,not false,fuck here
        if(pool->isDestroy==true)
        {
            pthread_mutex_destroy(&pool->queue_mutex);
            return ((void*)0);
        }
        cout<<"read workqueue\n";
        CThread_worker* handle_work=pool->queue_head;
        pool->queue_head=pool->queue_head->next;
        handle_work->next=NULL;
        pthread_mutex_unlock(&pool->queue_mutex);
        
        // protocol_handle function
        handle_work->process(handle_work->ptr,handle_work->sockfd);
        free(handle_work);
    }
    return ((void*)0);
}
