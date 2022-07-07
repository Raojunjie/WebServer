/*
@Author    : Raojunjie
@Date      : 2022-5-14
@Detail    : 线程池类，描述线程池的行为
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <exception>
#include <pthread.h>
#include "../locker/locker.h"
#include "../mysql/sqlpool.h"

/* 线程池类，功能：创建若干工作线程，不释放，存放在线程数组中；
   可以通过函数向请求队列添加请求，无请求，工作线程阻塞；有请求，解除阻塞，有信号量进行通知 */
template<typename T>
class ThreadPool{
public:
    ThreadPool(int actorMode, SqlPool* sqlPool, int threadNum = 8, int maxRequests = 10000);
    ~ThreadPool();
    bool append(T* request, int state);
    bool appendP(T* request);

private:
    static void* worker(void* arg);
    void run();

    int _threadNum;            /* 线程池中线程的数量 */
    int _maxRequests;          /* 请求队列所允许的最大请求数 */
    pthread_t* _threads;       /* 线程池数组 */
    std::list<T*> _workQueue;  /* 请求队列，未处理的请求集合 */
    Locker _locker;            /* 互斥锁，操作请求队列时上锁 */
    Sem _unsettledNum;         /* 是否有请求需要处理，未处理的请求数目 */
    SqlPool* _sqlPool;         /* 数据库 */
    int _actorMode;            /* 模型, 0:proactor; 1:reactor */
};

template<typename T>
ThreadPool<T>::ThreadPool(int actorMode, SqlPool* sqlPool, int threadNum, int maxRequests):
               _actorMode(actorMode), _sqlPool(sqlPool), _threadNum(threadNum), _maxRequests(maxRequests){
    if(threadNum <= 0 || maxRequests <= 0){
        throw std::exception();
    }
    
    _threads = new pthread_t[_threadNum];
    if(!_threads){
        throw std::exception();
    }

    /* 创建threadNum个线程,并设置线程分离 */
    for(int i=0; i<threadNum; ++i){
        if(pthread_create(_threads+i, NULL, worker, this) != 0 ){
            delete[] _threads;
            throw std::exception();
        }
        if(pthread_detach(_threads[i]) != 0){
            delete[] _threads;
            throw std::exception();
        }
    }
}

template<class T>
ThreadPool<T>::~ThreadPool(){
    delete[] _threads;
}

/*
* 功能：向请求队列中添加新请求
* 参数：
*       --request：新请求
*       --state：  新请求的状态
*/
template<class T>
bool ThreadPool<T>::append(T* request, int state){
    /* 上锁 */
    _locker.lock();
    /* 请求队列已满,无法添加新的请求 */
    if(_workQueue.size() >= _maxRequests){
        _locker.unlock();
        return false;
    }
    /* 设置请求状态，将其加入请求队列，同时将未处理的请求数+1 */
    request->_state = state;
    _workQueue.push_back(request);
    _locker.unlock();
    _unsettledNum.post();
    return true;
}

/*
* 功能：向请求队列中添加新请求
*       proactor模式调用，已经读取了数据，所以不需要设置请求状态
* 参数：
*       --request：新请求
*/
template<class T>
bool ThreadPool<T>::appendP(T* request){
        /* 上锁 */
    _locker.lock();
    /* 请求队列已满,无法添加新的请求 */
    if(_workQueue.size() >= _maxRequests){
        _locker.unlock();
        return false;
    }
    /* 将其加入请求队列*/
    _workQueue.push_back(request);
    _locker.unlock();
    /* 将未处理的请求数+1, 通过信号量通知，工作线程开始工作 */
    _unsettledNum.post();
    return true;
}

/*
* 功能：线程处理函数
* 参数：
*       --arg：线程池对象的指针
*/
template<class T>
void* ThreadPool<T>::worker(void* arg){
    ThreadPool* pool = (ThreadPool*)arg;
    pool->run();
    return pool;
}

/*
* 功能：请求处理函数
*/
template<class T>
void ThreadPool<T>::run(){
    /* 等待新的请求到来 */
    while(true){
        /* 无请求时，工作线程处于阻塞状态；有未处理的请求时，则解除阻塞，进行处理*/
        _unsettledNum.wait();
        _locker.lock();
        if(_workQueue.empty()){
            _locker.unlock();
            continue;
        }

        /* 取出新请求 */
        T* request = _workQueue.front();
        _workQueue.pop_front();
        _locker.unlock();
        if(request == NULL){
            continue;
        }

        if(1 == _actorMode){
            /* reactor */
            if(0 == request->_state){
                /* 读数据 */
                if(request->readOnce()){
                    /* 读取成功 */
                    request->_improv = 1;
                    ConnRAII mysqlCon(&request->_mysql, _sqlPool);
                    /* 进行数据处理 */
                    request->process();
                }
                else{
                    /* 读失败 */
                    request->_improv = 1;
                    request->_timerFlag = 1;
                }
            }
            else{
                /* 写数据 */
                if(request->write()){
                    /* 写成功 */
                    request->_improv = 1;
                }
                else{
                    /* 写失败 */
                    request->_improv = 1;
                    request->_timerFlag = 1;
                }
            }
        }
        else{
            /*  proactor */
            ConnRAII mysqlCon(&request->_mysql, _sqlPool);
            /* 进行数据处理 */
            request->process();
        }
    }
}

#endif


