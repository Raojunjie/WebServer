/*
@Author    : Raojunjie
@Date      : 2022-5-14
@Detail    : server类的辅助类
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#ifndef UTILS_H
#define UTILS_H

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
#include "../timer/twTimer.h"

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
    SortTimerWheel _timerList;  /* 定时器链表 */
};

#endif