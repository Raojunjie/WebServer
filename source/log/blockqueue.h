/*
@Author    : Raojunjie
@Date      : 2022-5-23
@Detail    : 阻塞队列类
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H

#include "../locker/locker.h"
using namespace std;

/* 阻塞队列， 存放要写入的日志内容 */
template<class  T>
class BlockQueue{
public:
    /* 创建阻塞队列,队列最大长度 */ 
    BlockQueue(int maxSize = 1000){
        if(maxSize <= 0){
            exit(-1);
        }
        _maxSize = maxSize;
        _buffer = new T[maxSize];
        _size = 0;
        _front = -1; /* 还未塞入内容，所以位置初始化为-1 */
        _back = -1;
    }
    
    ~BlockQueue(){
        _lock.lock();
        if(_buffer != NULL){
            delete[] _buffer;
        }
        _lock.unlock();
    }

    void clear();
    bool full();
    bool empty();
    bool front(T& value);
    bool back(T& value);
    int size();
    int maxSize();
    bool push(const T& item);
    bool pop(T& item);
    bool pop(T& item, int timeOut);
    
private:
    T* _buffer;     /* 队列缓冲区，一个数组，存放未被读取的内容 */
    int _size;      /* 队列中未被读取元素的个数 */
    int _maxSize;   /* 队列数组的最大容量 */
    int _front;     /* 第一个未被读取元素在数组中位置的前一个位置 */
    int _back;      /* 最后一个未被读取元素在数组中的位置 */
    Locker _lock;   /* 用来同步与互斥的锁 */
    Cond _cond;     /* 条件变量 */
};

/*
*功能：获得队列里元素个数
*返回值： int 队列元素个数
*/
template<class T>
int BlockQueue<T>::size(){
    int tmp = 0;
    _lock.lock();
    tmp = _size;
    _lock.unlock();
    return tmp;
}

/*
*功能：获得队列长度
*返回值： int 队列长度
*/
template<class T>
int BlockQueue<T>::maxSize(){
    int tmp = 0;
    _lock.lock();
    tmp = _maxSize;
    _lock.unlock();
    return tmp;
}

/*
*功能：清空队列里所有内容,也就是内容个数设置为0
*/
template<class T>
void BlockQueue<T>::clear(){
    _lock.lock();
    _size = 0;
    _front = -1;
    _back = -1;
    _lock.unlock();
}

/*
*功能：判断队列是否满了
*返回值： TRUE：满了；FALSE：未满。
*/
template<class T>
bool BlockQueue<T>::full(){
    _lock.lock();
    if(_size == _maxSize){
        _lock.unlock();
        return true;
    }
    _lock.unlock();
    return false;
}

/*
*功能：判断队列是否为空
*返回值： TRUE：空；FALSE：不空。
*/
template<class T>
bool BlockQueue<T>::empty(){
    _lock.lock();
    if(_size == 0){
        _lock.unlock();
        return true;
    }
    _lock.unlock();
    return false;
}

/*
*功能：获取队列的首元素
*参数： T& value: 传出参数，首元素被传入value
*返回值： TRUE：获取成功；FALSE：失败。
*/
template<class T>
bool BlockQueue<T>::front(T& value){
    _lock.lock();
    /* 队列里无元素,获取失败 */
    if(_size == 0){
        _lock.unlock();
        return false;
    }
    /* 有元素，返回front位置的元素 */
    value = _buffer[_front];
    _lock.unlock();
    return true;
}

/*
*功能：获取队列的尾元素
*参数： T& value: 传出参数，尾元素被传入value
*返回值： TRUE：获取成功；FALSE：失败。
*/
template<class T>
bool BlockQueue<T>::back(T& value){
    _lock.lock();
    /* 队列里无元素,获取失败 */
    if(_size == 0){
        _lock.unlock();
        return false;
    }
    /* 有元素，返回back位置的元素 */
    value = _buffer[_back];
    _lock.unlock();
    return true;
}

/*
*功能：向队列中添加元素，加入后将所有需要使用元素的队列唤醒
*参数： T& value: 传入参数，value被加入到队列中
*返回值： TRUE：成功；FALSE：失败。
*/
template<class T>
bool BlockQueue<T>::push(const T& value){
    _lock.lock();
    /* 队列已满，添加失败 */
    if(_size == _maxSize){
        /* 通知消费者消费 */
        _cond.broadcast();
        _lock.unlock();
        return false;
    }
    /* 队列有空闲位置，添加，先获得添加位置，即_back后一个
       因为队列是循环添加，所以采用取模的方式 */
    _back = (_back+1) % _maxSize;
    _buffer[_back] = value;
    /* 元素数量加1 */
    _size++;
    /* 通知消费者消费 */
    _cond.broadcast();
    _lock.unlock();
    return true;
}

/*
*功能：弹出队列的首元素，
*参数： T& value: 传出参数，value被弹出队列
*返回值： TRUE：成功；FALSE：失败。
*/
template<class T>
bool BlockQueue<T>::pop(T& value){
    _lock.lock();
    /* 队列为空 */
    while(_size == 0){
        /* 队列为空，则阻塞,等待队列中被加入元素 */
        if(!_cond.wait(_lock.getLock())){
            /* 失败 */
            _lock.unlock();
            return false;
        }
    }
    /* 队列有元素，先获得首元素的位置，即_front后一个
       因为队列是循环添加，所以采用取模的方式 */
    _front = (_front+1) % _maxSize;
    value = _buffer[_front];
    /* 元素数量减1 */
    _size--;
    _lock.unlock();
    return true;
}

/*
*功能：弹出队列的首元素，增加了超时处理，超时则不等待
*参数： T& value: 传出参数，value被弹出队列；timeOut: 超时时间,单位ms
*返回值： TRUE：成功；FALSE：失败。
*/
template<class T>
bool BlockQueue<T>::pop(T& value, int timeOut){
    struct timespec t = {0,0};
    struct timeval now = {0,0};
    gettimeofday(&now, NULL);

    _lock.lock();
    /* 队列为空 */
    if(_size == 0){
        t.tv_sec = now.tv_sec + timeOut / 1000;
        t.tv_nsec = (timeOut % 1000) * 1000;
        /* 阻塞,等待队列中被加入元素,或者超时也退出 */
        if(!_cond.timewait(_lock.getLock(), t)){
            _lock.unlock();
            return false;
        }
    }

    /* 超时，仍未有元素加入 */
    if(_size == 0){
        _lock.unlock();
        return false;
    }

    /* 队列有元素，先获得首元素的位置，即_front后一个
       因为队列是循环添加，所以采用取模的方式 */
    _front = (_front+1) % _maxSize;
    value = _buffer[_front];
    /* 元素数量减1 */
    _size--;
    _lock.unlock();
    return true;
}

#endif