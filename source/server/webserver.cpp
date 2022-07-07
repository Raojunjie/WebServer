#include "webserver.h"

WebServer::WebServer(){
    _usersHttp = new HttpConn[MAX_FD];

    /* 设置文件路径 = 当前路径/root */
    char serverPath[200];
    getcwd(serverPath,200);
    char root[6] = "/root";
    _root = (char*)malloc(strlen(serverPath) + strlen(root) + 1);
    strcpy(_root, serverPath);
    strcat(_root, root);
    /* 定时器 */
    _usersTimer = new ClientData[MAX_FD];

    _port = 9006;        /* 端口号默认是 9006 */
    _writeLog = 0;       /* 日志写入方式，默认同步 */
    _closeLog = 0;       /* 是否关闭日志功能，默认不关闭 */
    _trigMode = 0;       /* 触发组合模式，默认listenFd(LT)+httpFd(LT) */
    _listenTrigMode = 0; /*默认LT*/
    _httpTrigMode = 0;   /* 默认LT */
    _optLinger = 0;      /* 默认不使用优雅关闭连接 */
    _sqlNum = 8;         /* 默认sql连接池中有8个mysql连接 */ 
    _threadNum = 8;      /* 默认线程池中有8个线程 */
    _actorMode = 0;      /*事件处理模式，默认是Proactor */
}

WebServer::~WebServer(){
    close(_epollFd);
    close(_listenFd);
    close(_pipeFd[1]);
    close(_pipeFd[0]);
    delete[] _usersHttp;
    delete[] _usersTimer;
    delete _threadsPool;
}

/*
*  功能：设置sql的登录信息
*  参数：
*        --user: 用户名
*        --password: 密码
*        --database: 数据库名称
*/
void WebServer::setSql(string user,string password,string database){
    _user = user;
    _password = password;
    _database = database;
}

/*
*  功能：解析命令行，设置服务器的参数
*  参数：
*        --argc: argv的个数
*        --argv：命令行内容
*/
void WebServer::parseArgs(int argc, char** argv){
    int opt;
    const char* str = "p:l:m:o:s:t:c:a:";
    while((opt = getopt(argc,argv,str)) != -1){
        switch(opt){
            case 'p':
            {
                _port = atoi(optarg);
                break;
            }
            case 'l':
            {
                _writeLog = atoi(optarg);
                break;
            }
            case 'm':
            {
                _trigMode = atoi(optarg);
                break;
            }
            case 'o':
            {
                _optLinger = atoi(optarg);
                break;
            }
            case 's':
            {
                _sqlNum = atoi(optarg);
                break;
            }
            case 't':
            {
                _threadNum = atoi(optarg);
                break;
            }
            case 'c':
            {
                _closeLog = atoi(optarg);
                break;
            }
            case 'a':
            {
                _actorMode = atoi(optarg);
                break;
            }
            default: break;
        }
    }
}

/*
*  功能：初始化日志
*/
void WebServer::logWrite(){
    /* 使用日志功能 */
    if(0 == _closeLog){
        /* 1: 异步写入; 0: 同步写入 */
        if(1 == _writeLog)
            Log::getInstance()->init("./serverLog", _closeLog, 2000, 800000, 800);
        else
            Log::getInstance()->init("./serverLog", _closeLog, 2000, 800000, 0);
    }
}

/*
*  功能：创建sql连接池，初始化数据库读取表
*/
void WebServer::sqlPool(){
    _sqlPool = SqlPool::getInstance();
    _sqlPool->init("localhost",_user,_password,_database,3306,_sqlNum,_closeLog);
    _usersHttp->initMySQLResult(_sqlPool);
}

/*
*  功能：设置线程池
*/
void WebServer::threadPool(){
    _threadsPool = new ThreadPool<HttpConn>(_actorMode,_sqlPool,_threadNum);
}

/*
*  功能：设置epoll触发模式
*/
void WebServer::trigMode(){
    if(0 == _trigMode){
        /* LT + LT */
        _listenTrigMode = 0;
        _httpTrigMode = 0;
    }
    else if(1 == _trigMode){
        /* LT + ET */
        _listenTrigMode = 0;
        _httpTrigMode = 1;
    }
    else if(2 == _trigMode){
        /* ET + LT */
        _listenTrigMode = 1;
        _httpTrigMode = 0;
    }
    else if(3 == _trigMode){
        /* ET + ET */
        _listenTrigMode = 1;
        _httpTrigMode = 1;
    }
}

/*
*  功能：设置监听socket, 并设置信号函数
*/
void WebServer::eventListen(){

    /* 创建监听socket */
    _listenFd = socket(PF_INET, SOCK_STREAM, 0);
    assert(_listenFd >= 0);

    if(0 == _optLinger){
        /* 不使用优雅关闭连接，即close时会立即关闭，不会等待剩余数据的传送 */
        struct linger tmp = {0,1};
        setsockopt(_listenFd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }
    else{
        /* 使用优雅关闭连接，即close时会延时1秒，等待剩余数据的传送 */
        struct linger tmp = {1,1};
        setsockopt(_listenFd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }

    int ret = -1;
    /* 设置服务器监听地址 */
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(_port);

    int flag = 1;
    /* 设置端口复用 */
    setsockopt(_listenFd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));
    /* 将_listenFd与本地的IP+PORT绑定 */
    ret = bind(_listenFd,(struct sockaddr*)&address,sizeof(address));
    assert(ret >= 0);
    /* 监听 */
    ret = listen(_listenFd,5);
    assert(ret >= 0);

    _utils.init(TIME_SLOT);

    /* 创建epoll内核事件表 */
    _epollFd = epoll_create(1);
    assert(_epollFd != -1);
    /* 将监听socket加入epoll中 */
    _utils.addFd(_epollFd, _listenFd,false, _listenTrigMode);

    /* 创建一对socket,用于进程间通信 */
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, _pipeFd);
    assert(ret == 0);
    _utils.setNonblocking(_pipeFd[1]);
    _utils.addFd(_epollFd, _pipeFd[0], false, 0);

    /* 忽略SIGPIPE */
    _utils.addSig(SIGPIPE, SIG_IGN);
    /* 为避免信号竞态现象发生，信号处理期间系统不会再次触发它。
       所以信号需要被快速处理，这里信号处理函数只负责将信号
       通过管道传递给主循环，由主循环进行处理 */
    /* 处理SIGALRM，定时时间到 */
    _utils.addSig(SIGALRM, _utils.sigHandler, false);
    /* 处理SIGTERM, 终止信号 */
    _utils.addSig(SIGTERM, _utils.sigHandler, false);
    /* 设置闹钟 */
    alarm(TIME_SLOT);

    Utils::_pipeFd = _pipeFd;
    Utils::_epollFd = _epollFd;
    HttpConn::_epollFd = _epollFd;
}

/*
*  功能：处理到来的事件
*/
void WebServer::eventLoop(){
    bool timeOut = false;     /* 是否超时 */
    bool stopServer = false;  /* 是否停止服务 */

    while(!stopServer){
        /* 监测事件, 阻塞 */
        int number = epoll_wait(_epollFd, _events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR){
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        /* 处理I/O事件 */
        for(int i=0; i<number; i++){
            int sockFd = _events[i].data.fd;
            if(sockFd == _listenFd){
                /* 有新的连接 */
                bool flag = dealClientData();
                if(!flag)
                    continue;
            }
            else if(_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                /* 客户端关闭连接 */
                UtilTimer* timer = _usersTimer[sockFd]._timer;
                /* 调用回调函数（将sockFd移除epoll监听）, 删除对应的定时器 */
                dealTimer(timer, sockFd);
            }
            else if((sockFd == _pipeFd[0]) && (_events[i].events & EPOLLIN)){
                /* 处理信号 */
                dealWithSignal(timeOut, stopServer);
            }
            else if(_events[i].events & EPOLLIN){
                /* 客户连接上发来新的数据 */
                dealWithRead(sockFd);
            }
            else if(_events[i].events & EPOLLOUT){
                /* 写缓冲区由满变成未满，触发EPOLLOUT，将响应写回 */
                dealWithWrite(sockFd);
            }
        }
        /* 超时 */
        if(timeOut){
            _utils.timerHandler();
            LOG_INFO("%s","timer tick");
            timeOut = false;
        }
    }
}

/*
*  功能：处理新的连接
*  返回值：成功： true; 失败： false.
*/
bool WebServer::dealClientData(){
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    if(0 == _listenTrigMode){
        /* LT */
        int httpFd = accept(_listenFd, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if(httpFd == -1){
            LOG_ERROR("%s, errno is %d", "accept error", errno);
            return false;
        }
        if(HttpConn::_userCount >= MAX_FD){
            _utils.showError(httpFd, "Interval server is busy");
            LOG_ERROR("%s", "Interval server is busy");
            return false;
        }
        /* 初始化客户端连接数据 */
        _usersHttp[httpFd].init(httpFd, clientAddr, _root, _httpTrigMode, _closeLog, _user, _password, _database);
        /* 初始化定时器 */
        setTimer(httpFd, clientAddr);
    }
    else{
        /* ET */
        while(1){
            int httpFd = accept(_listenFd, (struct sockaddr*)&clientAddr, &clientAddrLen);
            if(httpFd < 0){
                if(errno != EAGAIN){
                    LOG_ERROR("%s, errno is %d", "accept error", errno);
                    return false;
                }
                break;
            }
            if(HttpConn::_userCount >= MAX_FD){
                _utils.showError(httpFd, "Interval server is busy");
                LOG_ERROR("%s", "Interval server is busy");
                return false;
            }
            /* 初始化客户端连接 */
            _usersHttp[httpFd].init(httpFd, clientAddr,_root, _httpTrigMode, _closeLog, _user,_password, _database);
            /* 初始化定时器 */
            setTimer(httpFd, clientAddr);
        }
    }
    return true;
}

/*
*  功能：绑定客户数据，创建定时器，设置回调函数和超时时间，将定时器加入链表
*  参数：
*       --httFd: 客户连接的socket
*       --addr: 客户地址
*/
void WebServer::setTimer(int httpFd, struct sockaddr_in addr){
    _usersTimer[httpFd]._address = addr;
    _usersTimer[httpFd]._sockFd = httpFd;
    _usersTimer[httpFd]._timer = _utils._timerList.addTimer(3*TIME_SLOT);
    _usersTimer[httpFd]._timer->_userData = &_usersTimer[httpFd];
    _usersTimer[httpFd]._timer->_cbFunc = cb_func;
}

/*
*  功能：调用定时器的回调函数关闭连接，将定时器移除链表
*  参数：
*       --timer: 需要处理的定时器
*       --sockFd: 与定时器对应的socket
*/
void WebServer::dealTimer(UtilTimer* timer, int sockFd){
    //timer->_cbFunc(&_usersTimer[sockFd]);
    timer->_cbFunc(timer->_userData);
    if(timer){
        _utils._timerList.deleteTimer(timer);
    }
    LOG_INFO("close fd %d", _usersTimer[sockFd]._sockFd);
}

/*
*  功能：信号来临，执行相应的处理
*       SIGALRM：表明有任务超时，设置超时，I/O读取结束，再删除超时任务
*       SIGTERM: 终止服务器
*  参数：
*       --timeOut: 传出参数，是否有超时任务
*       --stopServer：传出参数，是否终止服务器
*/
void WebServer::dealWithSignal(bool& timeOut, bool& stopServer){
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(_pipeFd[0], signals, sizeof(signals), 0);
    if(ret == -1 || ret == 0){
        LOG_ERROR("%s","read signal failed");
        return;
    }
    else{
        for(int i=0; i<ret; i++){
            switch(signals[i]){
                case SIGALRM:
                {
                    timeOut = true;
                    break;
                }
                case SIGTERM:
                {
                    stopServer = true;
                    break;
                }
            }
        }
    }
}

/*
*  功能：读取客户发送的数据
*  参数：
*       --sockFd: 客户连接socket
*/
void WebServer::dealWithRead(int sockFd){
   
   UtilTimer* timer = _usersTimer[sockFd]._timer;

   if(1 == _actorMode){
       /*  reactor */
        if(timer){
            /* 有数据传输，则连接重置活跃监测时间 */
            adjustTimer(timer);
        }
        /* 将读取事件放入请求队列 */
        _threadsPool->append(_usersHttp+sockFd, 0);
        
        /* 循环监测I/O事件是否被处理 */
        while(true){
            if(1 == _usersHttp[sockFd]._improv){
                /* 被处理了 */
                if(1 == _usersHttp[sockFd]._timerFlag){
                    /* 处理失败了 */
                    dealTimer(timer, sockFd);
                    _usersHttp[sockFd]._timerFlag = 0;
                }
                /* 重置 */
                _usersHttp[sockFd]._improv = 0;
                break;
            }
        }
   }
   else{
        /* proactor */
        /* 读取数据 */
        if(_usersHttp[sockFd].readOnce()){
            LOG_INFO("deal with the client(%s)", 
               inet_ntoa(_usersHttp[sockFd].getAddress()->sin_addr));
            
            /* 放入请求队列 */
            _threadsPool->appendP(_usersHttp + sockFd);
            
            if(timer){
                /* 有数据传输，则连接重置活跃监测时间 */
                adjustTimer(timer);
            }
        }
        else{
           /* 读失败，删除定时器，关闭socket */
           dealTimer(timer, sockFd);
        }
   }
}

/*
*  功能：向客户端发送数据
*  参数：
*       --sockFd: 客户连接的socket
*/
void WebServer::dealWithWrite(int sockFd){
    UtilTimer* timer = _usersTimer[sockFd]._timer;

    if(1 == _actorMode){
        /* reactor */
        /* 执行了I/O事件，活跃检测时间重置 */
        if(timer) adjustTimer(timer);
        /* 添加至请求队列 */
        _threadsPool->append(_usersHttp + sockFd, 1);

        while(true){
            if(1 == _usersHttp[sockFd]._improv){
                if(1 == _usersHttp[sockFd]._timerFlag){
                    dealTimer(timer, sockFd);
                    _usersHttp[sockFd]._timerFlag = 0;
                }
                _usersHttp[sockFd]._improv = 0;
                break;
            }
        }
    }
    else{
        /* proactor */
        /* 写数据 */
        if(_usersHttp[sockFd].write()){
            LOG_INFO("send data to the client(%s)",
               inet_ntoa(_usersHttp[sockFd].getAddress()->sin_addr));
            
            /* 执行了I/O事件，活跃检测时间重置 */
            if(timer) adjustTimer(timer);
        }
        else{
           /* 写失败，删除定时器，关闭socket */
           dealTimer(timer, sockFd);
        }
    }
}

/*
*  功能：有数据传输，则连接重置活跃监测时间
*  参数：
*       --timer: 需要处理的定时器
*/
void WebServer::adjustTimer(UtilTimer* timer){
    time_t cur = time(NULL);
    /* 重置后需要调整定时器在链表中的位置 */
    _utils._timerList.adjustTimer(timer, 3*TIME_SLOT);

    LOG_INFO("%s","reset timer once");
}