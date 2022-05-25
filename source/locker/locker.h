/*
@Author    : Raojunjie
@Date      : 2022-5-14
@Detail    : 互斥同步类，封装linux提供的互斥同步量（锁、条件变量和信号量），实现RAII
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>

/* 信号量类 */
class Sem{
public:
    Sem(){
        if(sem_init(&_sem,0,0) != 0 ){
            throw std::exception();
        }
    }

    Sem(int value){
        if(sem_init(&_sem,0,value) != 0){
            throw std::exception();
        }
    }

    ~Sem(){ sem_destroy(&_sem); }

    bool wait(){ return sem_wait(&_sem) == 0; }
    bool post(){ return sem_post(&_sem) == 0; }

private:
    sem_t _sem;
};

/* 互斥锁类 */
class Locker{
public:
    Locker(){
        if(pthread_mutex_init(&_mutex,NULL) != 0){
            throw std::exception();
        }
    }

    ~Locker(){ pthread_mutex_destroy(&_mutex); }
    bool lock(){ return pthread_mutex_lock(&_mutex) == 0; }
    bool unlock(){ return pthread_mutex_unlock(&_mutex) == 0; }
    pthread_mutex_t* getLock(){ return &_mutex; }

private:
    pthread_mutex_t _mutex;
};

/* 条件变量类 */
class Cond{
public:
    Cond(){
        if(pthread_cond_init(&_cond,NULL) != 0){
            throw std::exception();
        }
    }

    ~Cond(){pthread_cond_destroy(&_cond);}

    bool wait(pthread_mutex_t* mutex){
        return pthread_cond_wait(&_cond, mutex) == 0;
    }

    bool timewait(pthread_mutex_t* mutex, struct timespec t){
        return pthread_cond_timedwait(&_cond,mutex, &t) == 0;
    }

    bool signal(){
        return pthread_cond_signal(&_cond) == 0;
    }

    bool broadcast(){
        return pthread_cond_broadcast(&_cond) == 0;
    }
    
private:
    pthread_cond_t _cond;
};

#endif
