/*
@Author    : Raojunjie
@Date      : 2022-5-14
@Detail    : 服务器的类，描述服务器的行为
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdio.h>
#include <string.h>
#include <string>
#include <arpa/inet.h>
#include <cassert>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>
#include "../threadpool/threadpool.h"
#include "../http/httpconn.h"
#include "../timer/lstTimer.h"
#include "../log/log.h"
#include "../mysql/sqlpool.h"
using namespace std;

/* 文件描述符数量的最大值 */
const int MAX_FD = 10240;
/* 监听事件数量的最大值 */
const int MAX_EVENT_NUMBER = 10000;
/* 超时单位 */
const int TIME_SLOT = 5;

class WebServer{
public:
    WebServer();
    ~WebServer();

    void setSql(string user,string password,string database);
    void parseArgs(int argc, char** argv);
    void logWrite();
    void sqlPool();
    void threadPool();
    void trigMode();
    void eventListen();
    void eventLoop();

    bool dealClientData();
    void setTimer(int httpFd, struct sockaddr_in addr);
    void dealTimer(UtilTimer* timer, int sockFd);
    void dealWithSignal(bool& timeOut, bool& stopServer);
    void dealWithRead(int sockFd);
    void dealWithWrite(int sockFd);
    void adjustTimer(UtilTimer* timer);
public:
    int _port;          /* 端口号 */
    char* _root;        /* 文件路径，根目录 */
    int _writeLog;      /*日志写入方式，默认同步 */
    int _closeLog;      /* 关闭日志，默认不关闭 */
    int _actorMode;     /*事件处理模式，默认是Proactor */
    int _pipeFd[2];
    int _epollFd;       
    HttpConn* _usersHttp;  /* http连接数组 */

    SqlPool* _sqlPool;   /* 数据库连接池 */
    string _user;       /* 登录数据库的用户名 */
    string _password;   /* 密码 */
    string _database;   /* 数据库名 */
    int _sqlNum;        /* 数据库连接池数量 */

    ThreadPool<HttpConn>* _threadsPool;  /* 线程池 */
    int _threadNum;      /* 线程池中线程数量 */

    epoll_event _events[MAX_EVENT_NUMBER];  /* 监听事件数组 */

    int _listenFd;        /* 监听文件描述符 */
    int _optLinger;       /* 是否优雅关闭链接 */
    int _trigMode;        /* 触发组合模式，默认0:listenFd(LT)+httpFd(LT) */
    int _listenTrigMode;  /* listenFd触发模式,0:LT;1:ET */
    int _httpTrigMode;    /* http请求触发模式,0:LT;1:ET */

    ClientData* _usersTimer;
    Utils _utils;
};

#endif