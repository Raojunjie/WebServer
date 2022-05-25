/*
@Author    : Raojunjie
@Date      : 2022-5-14
@Detail    : 定时器的类
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#ifndef LST_TIMER_H
#define LST_TIMER_H

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
    UtilTimer():_prev(NULL), _next(NULL){}

public:
    time_t _expire;  /* 定时时间,这里使用绝对时间 */
    /* 使用链表，将各个连接的定时器串联起来 */
    UtilTimer* _prev;  /*前一个定时器 */
    UtilTimer* _next; /* 后一个定时器 */
    ClientData* _userData; 
    /* 任务回调函数 */
    void (* _cbFunc)(ClientData*);
};

/* 定时器链表, 按照时间升序组织 */
class SortTimerList{
public:
    SortTimerList();
    ~SortTimerList();
    void addTimer(UtilTimer* timer);
    void adjustTimer(UtilTimer* timer);
    void deleteTimer(UtilTimer* timer);
    void tick();
private:
    void addTimer(UtilTimer* timer, UtilTimer* head);
    UtilTimer* _head;
    UtilTimer* _tail;
};

/* 工具类, 可以优化 */
class Utils{
public:
    Utils(){}
    ~Utils(){}

    void init(int timeSlot);
    void addFd(int epollFd,int fd, bool oneShot, int trigMode);
    int setNonblocking(int fd);
    void addSig(int sig, void(handler)(int), bool restart = false);
    static void sigHandler(int sig);
    void showError(int fd, const char* info);
    void timerHandler();

public:
    static int* _pipeFd;   /* webserver中的_pipeFd */
    static int _epollFd;   /* webserver中的_epollFd */
    int _timeSlot;         /* 时间片，定时时间 */
    SortTimerList _timerList;  /* 定时器链表 */
};

/*
*功能：超时信号的回调函数，将用户从epoll监听数据中删除，并关闭
*参数：  --uesrData：超时的连接
*/
void cb_func(ClientData* userData);

#endif