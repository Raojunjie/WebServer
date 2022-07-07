#include "utils.h"

int Utils::_epollFd = 0;
int* Utils::_pipeFd = NULL;

void Utils::init(int timeSlot){
   _timeSlot = timeSlot;
}

/*
*  功能：将fd添加至epollfd的监听序列
*  参数：
*        --epollFd: epoll内核事件表的文件描述符
*        --fd: 需要添加的socket
*        --oneShot: 事件类型，是否是EPOLLONESHOT
*        --trigMode: 事件触发模式
*/
void Utils::addFd(int epollFd,int fd, bool oneShot, int trigMode){
    epoll_event event;
    event.data.fd = fd;

    /* epoll默认是LT模式
       EPOLLIN: 对应文件可以读；
       EPOLLRDHUP: 对方关闭连接；
       EPOLLET: 使用ET模式 */
    if(1 == trigMode)
       event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    else
       event.events = EPOLLIN | EPOLLRDHUP;
    /* EPOLLONESHOT: 同时只能一个线程处理事件，这次事件后，
       若仍需监听，需要重新将socketfd加入监听序列 */
    if(oneShot)
       event.events |= EPOLLONESHOT;
    /* 将fd添加至epollfd */
    epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event);
    setNonblocking(fd);
}

/*
*  功能：将fd设置为非阻塞
*  参数：
*        --fd: 需要设置的文件描述符
*  返回值：文件的原状态
*/
int Utils::setNonblocking(int fd){
   int oldOption = fcntl(fd,F_GETFL);
   int newOption = oldOption | O_NONBLOCK;
   fcntl(fd,F_SETFL,newOption);
   return oldOption;
}

/*
*  功能：设置信号函数
*  参数：
*        --sig: 信号
*        --handler: 信号处理函数
*        --restart: 是否重启，如果信号中断了系统调用，操作系统会自动重启该系统调用
*/
void Utils::addSig(int sig, void(handler)(int), bool restart){
   struct sigaction sa;
   memset(&sa, '\0', sizeof(sa));
   sa.sa_handler = handler;
   if(restart)
      sa.sa_flags |= SA_RESTART;
   sigfillset(&sa.sa_mask);
   assert(sigaction(sig, &sa, NULL) != -1);
}

/*
*  功能：信号处理函数, 发送信号
*  参数：
*        --sig: 信号
*/
void Utils::sigHandler(int sig){
   int oldErrno = errno;
   int msg = sig;
   /* 发送信号 */
   send(_pipeFd[1], (char*)&msg, 1, 0);
   errno = oldErrno;
}

/*
*  功能：向fd发送错误信息
*  参数：
*        --fd: 目的地
*        --info: 信息
*/
void Utils::showError(int fd, const char* info){
   send(fd, info, strlen(info), 0);
   close(fd);
}

/*
*  功能：SIGALRM的信号处理函数，执行tick函数，并重新定时
*/
void Utils::timerHandler(){
   _timerList.tick();
   alarm(_timeSlot);
}
