#include "lstTimer.h"

SortTimerList::SortTimerList(){
   _head = NULL;
   _tail = NULL;
}

SortTimerList::~SortTimerList(){
   UtilTimer* tmp = _head;
   while(tmp){
      _head = tmp->_next;
      delete tmp;
      tmp = _head;
   }
}

/*
*  功能：将定时器加入链表
*  参数：
*        --timer： 定时器
*/
void SortTimerList::addTimer(UtilTimer* timer){
   if(timer == NULL) return;
   if(_head == NULL){
      _head = _tail = timer;
      return;
   }
   /* timer的时间最短，则添加至链表头 */
   if(timer->_expire < _head->_expire){
      timer->_next = _head;
      _head->_prev = timer;
      _head = timer;
      return;
   }
   /* 其他情况 */
   addTimer(timer, _head);
}

/*
*  功能：调整定时器位置，目前只考虑定时时间被延长的情况
*  参数：
*        --timer： 需要调整的定时器
*/
void SortTimerList::adjustTimer(UtilTimer* timer){
   if(timer == NULL) return;
   UtilTimer* tmp = timer->_next;
   /* 定时器处于尾部 或者 延长后的时间仍小于下一个定时器时间 */
   if(tmp == NULL || (timer->_expire < tmp->_expire))  return;
   /* 定时器位于头部，取出定时器，重新添加 */
   if(timer == _head){
      _head = _head->_next;
      _head->_prev = NULL;
      timer->_next = NULL;
      addTimer(timer,_head);
   }
   else{
      /* 不是头节点，取出，重新添加 */
      timer->_prev->_next = timer->_next;
      timer->_next->_prev = timer->_prev;
      addTimer(timer, timer->_next);
   }
}
    
/*
*  功能：删除定时器
*  参数：
*        --timer： 需要删除的定时器
*/
void SortTimerList::deleteTimer(UtilTimer* timer){
   if(timer == NULL) return;
   /* 链表中只有一个节点 */
   if(timer==_head && timer==_tail){
      delete timer;
      _head = _tail = NULL;
      return;
   }
   /* timer是头结点 */
   if(timer == _head){
      _head = _head->_next;
      _head->_prev = NULL;
      delete timer;
      return;
   }
   /* timer是尾结点 */
   if(timer == _tail){
      _tail = _tail->_prev;
      _tail->_next = NULL;
      delete timer;
      return;
   }
   /* 中间节点 */
   timer->_prev->_next = timer->_next;
   timer->_next->_prev = timer->_prev;
   delete timer;
}


/*
*  功能：SIGALRM被触发时，信号处理函数就会执行一次tick函数，
*       遍历处理链表上到期任务, 并删除到期的定时器
*/    
void SortTimerList::tick(){
   if(_head == NULL) return;
   time_t cur = time(NULL);
   UtilTimer* tmp = _head;
   while(tmp){
      /* 未到期 */
      if(cur < tmp->_expire){
         break;
      }
      /* 到期 */
      tmp->_cbFunc(tmp->_userData);
      _head = tmp->_next;
      if(_head){
         _head->_prev = NULL;
      }
      delete tmp;
      tmp = _head;
   }
}

/*
*  功能：将定时器timer加入链表, 
*        当定时器timer的时间长于定时器head的时间时，使用该函数
*  参数：
*        --timer： 需要添加的定时器
*        --head: 链表的头节点
*/
void SortTimerList::addTimer(UtilTimer* timer, UtilTimer* head){
   /* 遍历插入 */
   UtilTimer* prev = head;
   UtilTimer* tmp = prev->_next;
   while(tmp){
      if(timer->_expire < tmp->_expire){
         prev->_next = timer;
         timer->_next = tmp;
         tmp->_prev = timer;
         timer->_prev = prev;
         break;
      }
      prev = tmp;
      tmp = tmp->_next;
   }
   /* 没找到位置，插入末尾 */
   if(tmp == NULL){
      prev->_next = timer;
      timer->_prev = prev;
      timer->_next = NULL;
      _tail = timer;
   }
}

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

/*
*功能：超时信号的回调函数，将用户从epoll监听数据中删除，并关闭
*参数：  --uesrData：超时的连接
*/
void cb_func(ClientData* userData){
    epoll_ctl(Utils::_epollFd, EPOLL_CTL_DEL, userData->_sockFd, NULL);
    assert(userData);
    close(userData->_sockFd);
    HttpConn::_userCount--;
}