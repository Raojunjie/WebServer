/*
@Author    : Raojunjie
@Date      : 2022-7-7
@Detail    : 定时器的类,时间轮，使用哈希表代替链表
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#ifndef WHEEL_TIMER_H
#define WHEEL_TIMER_H

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <cassert>
#include <errno.h>
#include <arpa/inet.h>
#include <time.h>
#include <cassert>
#include "../log/log.h"
#include "../http/httpconn.h"

class UtilTimer;

struct ClientData{
    sockaddr_in _address;   /* 用户地址 */
    int _sockFd;     /* 连接fd */
    UtilTimer* _timer;   /* 定时器 */
};

/* 定时器类 */
class UtilTimer{
public:
    UtilTimer(int rot, int time_slot):_rotation(rot), _slot(time_slot), _prev(NULL), _next(NULL){}

public:
    int _rotation;  /* 定时时间对应多少圈时间轮 */
    int _slot;      /* 定时器挂在哪个时间槽中 */
    /* 使用链表，将各个连接的定时器串联起来 */
    UtilTimer* _prev;  /*前一个定时器 */
    UtilTimer* _next; /* 后一个定时器 */
    ClientData* _userData; 
    /* 任务回调函数 */
    void (* _cbFunc)(ClientData*);
};

/* 定时器链表, 按照时间升序组织 */
class SortTimerWheel{
public:
    SortTimerWheel();
    ~SortTimerWheel();
    UtilTimer* addTimer(int time);
    void deleteTimer(UtilTimer* timer);
    void adjustTimer(UtilTimer* timer, int time);
    void tick();
private:
    static const int N = 60;  /* 时间轮上的槽数 */
    static const int SI = 5;  /* 时针转动一次的时间, 单位s */
    int _curSlot;             /* 时针指向的当前槽 */
    UtilTimer* _slots[N];     /* 时间轮数组，存放定时器 */
};

/*
*功能：超时信号的回调函数，将用户从epoll监听数据中删除，并关闭
*参数：  --uesrData：超时的连接
*/
void cb_func(ClientData* userData);

#endif