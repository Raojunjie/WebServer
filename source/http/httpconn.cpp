#include "httpconn.h"
#include <iostream>
Locker _locker;

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

int HttpConn::_epollFd = -1;  /* epoll的文件描述符 */
int HttpConn::_userCount = 0; /* 已连接的客户数量 */

map<string,string> _users;   /* sql中的用户 */

/*
*功能：初始化连接, 并将连接加入epoll中
*参数：
*    --sockFd: 连接的文件描述符
*    --addr: 客户端地址
*    --root: 文件路径
*    --trigMode: 触发模式
*    --closeLog: 日志文件关闭模式
*    --user，password, database: sql参数
*/
void HttpConn::init(int sockFd, const sockaddr_in& addr, char* root, int trigMode,
    int closeLog, string user, string password, string database){
    _sockFd = sockFd;
    _address = addr;
 
    /* 当浏览器出现连接重置时 */
    _root = root;
    _trigMode = trigMode;
    _closeLog = closeLog;
    strcpy(_sqlUser, user.c_str());
    strcpy(_sqlPassword, password.c_str());
    strcpy(_sqlDatabase, database.c_str());

    /* 将连接加入epoll监听中 */
    addFd(_epollFd, _sockFd, true, _trigMode);
    _userCount++;
    init();
}

/*
*功能：初始化连接
*/
void HttpConn::init(){
    _mysql = NULL;
    _bytesToSend = 0;
    _bytesHaveSend = 0;
    _checkState = REQUESTLINE;
    _linger = false;
    _method = GET;
    _url = 0;
    _version = 0;
    _host = 0;
    _contentLen = 0;
    _startLine = 0;
    _checkedIdx = 0;
    _readIdx = 0;
    _writeIdx = 0;
    _cgi = 0;
    _state = 0;
    _timerFlag = 0;
    _improv = 0;

    memset(_readBuf, '\0', READ_BUFFER_SIZE);
    memset(_writeBuf, '\0', WRITE_BUFFER_SIZE);
    memset(_realFile, '\0', FILENAME_LEN);
}

/*
*  功能：将fd添加至epollfd的监听序列
*  参数：
*        --epollFd: epoll内核事件表的文件描述符
*        --fd: 需要添加的socket
*        --oneShot: 事件类型，是否是EPOLLONESHOT
*        --trigMode: 事件触发模式
*/
void HttpConn::addFd(int epollFd, int sockFd, bool oneShot, int trigMode){
    epoll_event event;
    event.data.fd = sockFd;

    if(1 == trigMode){
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    else{
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if(oneShot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollFd, EPOLL_CTL_ADD, sockFd, &event);
    setNonblocking(sockFd);
}

/*
*功能: 设置文件描述符为非阻塞
*参数：--fd: 需要设置的文件描述符
*返回值：返回设置前的文件状态
*/
int HttpConn::setNonblocking(int fd){
    int oldOtptions = fcntl(fd, F_GETFL);
    int newOptions = oldOtptions | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOptions);
    return oldOtptions;
}

/*
*  功能：将fd移除epollfd的监听序列
*  参数：
*        --epollFd: epoll内核事件表的文件描述符
*        --fd: 需要添加的socket
*/
void HttpConn::removeFd(int epollFd, int fd){
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/*
*  功能：将fd的事件重置为ONESHOT
*  参数：
*        --epollFd: epoll内核事件表的文件描述符
*        --fd: 需要重置的socket
*        --ev: fd事件的状态，读还是写
*        --trigMode: 事件触发模式
*/
void HttpConn::modFd(int epollFd, int fd, int ev, int trigMode){
    epoll_event event;
    event.data.fd = fd;
    if(1 == trigMode){
        event.events = ev | EPOLLET | EPOLLRDHUP;
    }
    else{
        event.events = ev | EPOLLRDHUP;
    }
    event.events |= EPOLLONESHOT;
    epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &event);
}

/*
*功能: 读取客户发送的数据
*返回值：是否读取成功
*/
bool HttpConn::readOnce(){
    
    /* 读缓冲区已满 */
    if(_readIdx >= READ_BUFFER_SIZE) return false;
    
    int bytesRead = 0;

    if(0 == _trigMode){
        /* LT */
        bytesRead = recv(_sockFd, _readBuf+_readIdx,
            READ_BUFFER_SIZE-_readIdx, 0);
        /* 接收数据出错 */
        if(bytesRead <= 0) return false;
        
        _readIdx += bytesRead;
    }
    else{
        /* ET */
        while(true){
            /* 接收数据存放在读缓冲区中 */
            bytesRead  = recv(_sockFd, _readBuf+_readIdx, 
                READ_BUFFER_SIZE-_readIdx, 0);
            if(bytesRead == -1){
                /* 没有数据了 */
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    break;
                }
                /* 读失败了 */
                return false;
            }
            else if(bytesRead == 0){
                /* 对方关闭连接 */
                return false;
            }
            _readIdx += bytesRead;
        }
    }
    return true;
}

/*
*功能: EPOLLOUT被触发，将内核写缓冲区由满变为未满，将用户写缓冲区的数据发送出去
*返回值：true: 连接继续存在；FALSE： 连接需要被关闭
*/
bool HttpConn::write(){
    int tmp = 0;
    if(_bytesToSend == 0){
        /* 待发送字节数为0，则响应结束，重置socket */
        modFd(_epollFd, _sockFd, EPOLLIN, _trigMode);
        init();
        return true;
    }
    while(true){
        /* 通过_sockFd向客户端发送数据，依次发送iv[0],iv[1],返回发送的字节数 */
        tmp = writev(_sockFd, _iv, _ivCount);
        if(tmp < 0){
            /* 写缓冲区满了，则重新注册EPOLLOUT事件，重置EPOLLONESHOT */
            if(errno == EAGAIN){
                modFd(_epollFd, _sockFd, EPOLLOUT, _trigMode);
                return true;
            }
            /* 发送失败，取消内存映射 */
            unmap();
            return false;
        }

        _bytesHaveSend += tmp;
        _bytesToSend -= tmp;
        /* 如果把iv[0]里的内容发送完了， 更新下次发送也就是iv[1]的起始地址和长度 */
        if(_bytesHaveSend >= _iv[0].iov_len){
            _iv[0].iov_len = 0;
            _iv[1].iov_base = _fileAddress + (_bytesHaveSend - _writeIdx);
            _iv[1].iov_len = _bytesToSend;
        }
        else{
            /* iv[0]没有发送完 */
            _iv[0].iov_base = _writeBuf+_bytesHaveSend;
            _iv[0].iov_len = _iv[0].iov_len - _bytesHaveSend;
        }

        /* 没有数据发送了 */
        if(_bytesToSend <= 0){
            unmap();
            /* 不再注册EPOLLOUT, 重置EPOLLONESHOT */
            modFd(_epollFd, _sockFd, EPOLLIN, _trigMode);
            /* 如果保持连接 */
            if(_linger){
                /* 本次请求结束，重新初始化http对象 */
                init();
                return true;
            }
            else{
                return false;
            }
        }
    }
}

/*
*功能: 取消资源文件的内存映射
*/
void HttpConn::unmap(){
    if(_fileAddress){
        munmap(_fileAddress, _fileStat.st_size);
        _fileAddress = NULL;
    }
}

/*
*功能: 读取数据后，处理客户端的请求，并将响应报文写入用户缓冲区，准备发送
*/
void HttpConn::process(){
    /* 解析http请求 */
    HTTP_CODE readRet = processRead();
    if(readRet == NO_REQUEST){
        /* 客户端没有请求, 响应结束，重置EPOLLONESHOT */
        modFd(_epollFd, _sockFd, EPOLLIN, _trigMode);
        return;
    }
    /* 有请求，则根据请求将响应报文写入用户缓冲区，之后再一次发送，减少调用 */
    bool writeRet = processWrite(readRet);
    if(!writeRet){
        /* 向写缓冲区写入失败 */
        closeConn();
    }
    /* 用户写缓冲有数据待发送，注册EPOLLOUT，重置EPOLLONESHOT */
    modFd(_epollFd, _sockFd, EPOLLOUT, _trigMode);
}

/*
*功能: 关闭连接
*/
void HttpConn::closeConn(bool close){
    if(close && _sockFd != -1){
        printf("close %d\n", _sockFd);
        removeFd(_epollFd, _sockFd);
        _sockFd = -1;
        _userCount--;
    }
}

/*
*功能: 读取数据后，解析客户端发来的请求报文
*/
HttpConn::HTTP_CODE HttpConn::processRead(){
    LINE_STATE lineState = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    /* parseLine() 返回该行内容已准备好 或者
      处于消息体（消息体没有\r\n）并且消息体没有处理完（处理完会变为LINE_OPEN) */
    while((_checkState == CONTENT && lineState == LINE_OK)
       || ( (lineState=parseLine()) == LINE_OK ) ){
        
        /*获取一行数据 */
        text = getLine();
        _startLine = _checkedIdx;
        LOG_INFO("%s", text);

        switch(_checkState){
            /* 请求行 */
            case REQUESTLINE:{
                ret = parseRequestLine(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            /* 请求头 */
            case HEADER:{
                ret = parseHeaders(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST){
                    /* 响应请求 */
                    return doRequest();
                }
                break;
            }
            case CONTENT:{
                ret = parseContent(text);
                if(ret == GET_REQUEST){
                    return doRequest();
                }
                lineState = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

/*
*功能: 在HTTP报文中，每一行的数据由\r\n作为结束字符，空行则是仅仅是字符\r\n。
       因此，可以通过查找\r\n将报文拆解成单独的行进行解析。
       从状态机负责读取buffer中的数据，将每行数据末尾的\r\n置为\0\0，并更新在buffer中读取的位置_checkedIdx。
       从状态机从_readBuf中逐字节读取，判断当前字节是否为\r
       -- 是\r
          - 接下来的字符是\n，将\r\n修改成\0\0，将_checkedIdx指向下一行的开头，则返回LINE_OK
          - 接下来达到了buffer末尾，表示buffer还需要继续接收，返回LINE_OPEN
          - 否则，表示语法错误，返回LINE_BAD
       -- 当前字节不是\r,是\n
          - 如果前一个字符是\r，则将\r\n修改成\0\0，将_checkedIdx指向下一行的开头，则返回LINE_OK
          - 否则，表示语法错误，返回LINE_BAD
       -- 当前字节既不是\r，也不是\n
          - 表示接收不完整，需要继续接收，返回LINE_OPEN
*返回值：该行内容的状态
*        - LINE_OK:   内容完整；
*        - LINE_OPEN: 内容不完整，需要继续接收；
*        - LINE_BAD:  内容中出现语法错误。
*/
HttpConn::LINE_STATE HttpConn::parseLine(){
    char tmp;
    for(; _checkedIdx < _readIdx; ++_checkedIdx){
        tmp = _readBuf[_checkedIdx];
        if(tmp == '\r'){
            if((_checkedIdx + 1) == _readIdx){
                return LINE_OPEN;
            }
            else if(_readBuf[_checkedIdx + 1] == '\n'){
                _readBuf[_checkedIdx++] = '\0';
                _readBuf[_checkedIdx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(tmp == '\n'){
            if(_checkedIdx > 1 && _readBuf[_checkedIdx - 1] == '\r'){
                _readBuf[_checkedIdx - 1] = '\0';
                _readBuf[_checkedIdx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

/*
*功能: 解析http请求报文的请求行：请求方法 url http版本号
*参数：text 请求行字符串首地址
*返回值：http状态码
*/
HttpConn::HTTP_CODE HttpConn::parseRequestLine(char* text){
    /* 找到第一个空格的位置 */
    _url = strpbrk(text, " \t");
    if(_url == NULL){
        return BAD_REQUEST;
    }
    /* 设置字符串结束符，得到请求方法，目前只支持GET和POST */
    *_url++ = '\0';
    char* method = text;
    if(strcasecmp(method, "GET") == 0){
        _method = GET;
    }
    else if(strcasecmp(method, "POST") == 0){
        _method = POST;
        _cgi = 1;
    }
    else{
        return BAD_REQUEST;
    }
    /* 跳过空格和制表符 */
    _url += strspn(_url, " \t");
    /* 找到url和版本号之间的空格 */
    _version = strpbrk(_url, " \t");
    if(_version == NULL){
        return BAD_REQUEST;
    }
    /* 设置字符串结束符，分割url和版本号 */
    *_version++ = '\0';
    _version += strspn(_version, " \t");
    /*  仅支持http/1.1 */
    if(strcasecmp(_version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }
    /* 比较前7个字符，是不是http */
    if(strncasecmp(_url, "http://", 7) == 0){
        _url += 7;
        /* 跳过IP地址和端口号，获得要访问资源的路径"/path" */
        _url = strchr(_url, '/');
    }
    /* 比较前8个字符，是不是https */
    if(strncasecmp(_url, "https://", 8) == 0){
        _url += 8;
        /* 跳过IP地址和端口号，获得要访问资源的路径 */
        _url = strchr(_url, '/');
    }
    if(_url == NULL || _url[0] != '/'){
        return BAD_REQUEST;
    }
    /* 当url为 / 时，显示判断界面 */
    if(strlen(_url) == 1){
        strcat(_url, "judge.html");
    }
    /* 请求行处理完毕，接下来出力首部行，所以更改主状态机状态 */
    _checkState = HEADER;
    return NO_REQUEST;
}

/*
*功能: 解析http请求报文的首部行的一行，首部字段名：值 或者是空行
*参数：text 首部行字符串首地址
*返回值：http状态码
*/
HttpConn::HTTP_CODE HttpConn::parseHeaders(char* text){
    if(text[0] == '\0'){
        /*  空行,说明首部行已经解析完毕 */
        if(_contentLen != 0){
            /* 是POST,转到CONTENT状态 */
            _checkState = CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0){
        /*  Connection */
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0){
            /* 保持长连接 */
            _linger = true;
        }
    }
    else if(strncasecmp(text, "Content-length:", 15) == 0){
        /* content内容长度 */
        text += 15;
        text += strspn(text, " \t");
        _contentLen = atol(text);
    }
    else if( strncasecmp(text, "Host:", 5) == 0){
        /* 客户端主机ip */
        text += 5;
        text += strspn(text, " \t");
        _host = text;
    }
    else {
        LOG_INFO("oop! unknown header: %s",text);
    }
    return NO_REQUEST;
}

/*
*功能: 读入http请求的内容content
*参数：text 内容字符串首地址
*返回值：http状态码
*/
HttpConn::HTTP_CODE HttpConn::parseContent(char* text){
    if(_contentLen + _checkedIdx <= _readIdx){
        /* 读缓冲区中有content */
        text[_contentLen] = '\0';
        _content = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/*
*功能: _url为请求报文中解析出的请求资源，以/开头，也就是/xxx，项目中解析后的m_url有8种情况。
       -- /
            GET请求，跳转到judge.html，即欢迎访问页面
       -- /0
            POST请求，跳转到register.html，即注册页面
       -- /1
            POST请求，跳转到log.html，即登录页面
       -- /2CGISQL.cgi
            POST请求，进行登录校验
            验证成功跳转到welcome.html，即资源请求成功页面
            验证失败跳转到logError.html，即登录失败页面
       -- /3CGISQL.cgi
            POST请求，进行注册校验
            注册成功跳转到log.html，即登录页面
            注册失败跳转到registerError.html，即注册失败页面
       -- /5
            POST请求，跳转到picture.html，即图片请求页面
       -- /6
            POST请求，跳转到video.html，即视频请求页面
       -- /7
            POST请求，跳转到fans.html，即关注页面
*返回值：http状态码
*/
HttpConn::HTTP_CODE HttpConn::doRequest(){
    /* 复制资源路径到响应文件路径名 */
    strcpy(_realFile, _root);
    int len = strlen(_root);
    const char* p = strchr(_url,'/');

    LOG_INFO("cgi: %d", _cgi);

    //cgi,登录校验和注册校验
    if(_cgi == 1 && (*(p+1) == '2' || *(p+1) == '3')){
        char* urlReal = (char*)malloc(sizeof(char)*FILENAME_LEN);
        strcpy(urlReal, "/");
        strcat(urlReal, _url+2);
        strncpy(_realFile+len, urlReal, FILENAME_LEN);
        free(urlReal);

        /* 提取用户名和密码 */
        /* user=zhangsan&password=123345 */
        char name[100],password[100];
        int i;
        for(i=5; _content[i]!='&'; i++){
            name[i-5] = _content[i];
        }
        name[i-5] = '\0';
        int j = 0;
        for(i=i+10; _content[i]!='\0'; ++i,++j){
            password[j] = _content[i];
        }
        password[j] = '\0';

        if(*(p+1) == '3'){
            /*  注册校验, 数据库中是否存在重名的 */
            char* sqlInsert = (char*)malloc(sizeof(char)*FILENAME_LEN);
            strcpy(sqlInsert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sqlInsert, "'");
            strcat(sqlInsert, name);
            strcat(sqlInsert, "', '");
            strcat(sqlInsert, password);
            strcat(sqlInsert, "')");

            if(_users.find(name) == _users.end()){
                _locker.lock();
                int ret = mysql_query(_mysql, sqlInsert);
                _users.insert(pair<string,string>(name,password));

                _locker.unlock();
                if(!ret){
                    strcpy(_url, "/log.html");
                }
                else{
                    strcpy(_url, "/registerError.html");
                }
            }
            else{
                strcpy(_url, "/registerError.html");
            }
        }
        else if(*(p+1) == '2'){
            /* 登录校验 */
            if(_users.find(name) != _users.end() && _users[name] == password){
                strcpy(_url, "/welcome.html");
            }
            else{
                strcpy(_url, "/logError.html");
            }
        }
    }
    if(*(p+1) == '0'){
        /* POST请求，响应为register.html */
        char* urlReal = (char*)malloc(sizeof(char)*FILENAME_LEN);
        strcpy(urlReal, "/register.html");
        strncpy(_realFile+len, urlReal, strlen(urlReal));
        free(urlReal);
    }
    else if(*(p+1) == '1'){
        /* POST请求,响应为log.html */
        char* urlReal = (char*)malloc(sizeof(char)*FILENAME_LEN);
        strcpy(urlReal, "/log.html");
        strncpy(_realFile+len, urlReal, strlen(urlReal));
        free(urlReal);
    }
    else if(*(p+1) == '5'){
        /* POST请求,响应为picture.html */
        char* urlReal = (char*)malloc(sizeof(char)*FILENAME_LEN);
        strcpy(urlReal, "/picture.html");
        strncpy(_realFile+len, urlReal, strlen(urlReal));
        free(urlReal);
    }
    else if(*(p+1) == '6'){
        /* POST请求,响应为video.html */
        char* urlReal = (char*)malloc(sizeof(char)*FILENAME_LEN);
        strcpy(urlReal, "/video.html");
        strncpy(_realFile+len, urlReal, strlen(urlReal));
        free(urlReal);
    }
    else if(*(p+1) == '7'){
        /* POST请求,响应为fans.html */
        char* urlReal = (char*)malloc(sizeof(char)*FILENAME_LEN);
        strcpy(urlReal, "/picture.html");
        strncpy(_realFile+len, urlReal, strlen(urlReal));
        free(urlReal);
    }
    else{
        /* 其他请求不处理 */
        strncpy(_realFile+len, _url, FILENAME_LEN-len-1);
    }
    
    /* 资源是否存在 */
    if(stat(_realFile, &_fileStat) < 0){
        return NO_RESOURCE;
    }
    /* 是否有可读权限 */
    if(!(_fileStat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }
    /* 是否是文件夹 */
    if(S_ISDIR(_fileStat.st_mode)){
        return BAD_REQUEST;
    }
    
    /* 将要访问的资源文件映射到内存 */
    int fd = open(_realFile, O_RDONLY);
    _fileAddress = (char*)mmap(0, _fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

/*
*功能: 根据do_request的返回状态，服务器子线程调用processWrite向_writeBuf中写入响应报文。
       并且放入_iv[0]和_iv[1]中.
*参数：code: http状态码
*返回值：是否写成功
*/
bool HttpConn::processWrite(HTTP_CODE code){
    switch(code){
        /* 内部错误，状态码 500 */
        case INTERNAL_ERROR:
        {
            addStatusLine(500, error_500_title);
            addHeaders(strlen(error_500_form));
            if(addContent(error_500_form) == false){
                return false;
            }
            break;
        }
        /* 请求报文语法错，状态码 404 */
        case BAD_REQUEST:
        {
            addStatusLine(404, error_404_title);
            addHeaders(strlen(error_404_form));
            if(addContent(error_404_form) == false){
                return false;
            }
            break;
        }
        /* 没有读权限，状态码 403 */
        case FORBIDDEN_REQUEST:
        {
            addStatusLine(403, error_403_title);
            addHeaders(strlen(error_403_form));
            if(addContent(error_403_form) == false){
                return false;
            }
            break;
        }
        /* 请求成功，状态码 200 */
        case FILE_REQUEST:
        {
            addStatusLine(200, ok_200_title);
            if(_fileStat.st_size != 0){
                /* 有需要返回的资源 */
                addHeaders(_fileStat.st_size);
                /* iv[0] 用来装响应报文的状态行和首部行；
                   iv[1] 用来装响应报文的实体消息。 */
                _iv[0].iov_base = _writeBuf;
                _iv[0].iov_len = _writeIdx;
                _iv[1].iov_base = _fileAddress;
                _iv[1].iov_len = _fileStat.st_size;
                _ivCount = 2;
                _bytesToSend = _iv[0].iov_len+_iv[1].iov_len;
                return true;
            }
            else{
                /* 没有需要返回的，返回空 */
                const char* okString = "<html><body></body></html>";
                addHeaders(strlen(okString));
                if(addContent(okString) == false){
                    return false;
                }
            }
        }
        default:
            return false;
    }
    /* 除FILE_REQUEST外，输出都放入_iv[0] */
    _iv[0].iov_base = _writeBuf;
    _iv[0].iov_len = _writeIdx;
    _ivCount = 1;
    _bytesToSend = _iv[0].iov_len;
    return true;
}

/*
*功能: 将响应报文写入_writeBuf
*参数：format：需要写入的内容的格式
*/
bool HttpConn::addResponse(const char* format, ...){
    if(_writeIdx >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    /* 将内容写入_writeBuf */
    int len = vsnprintf(_writeBuf+_writeIdx, 
        WRITE_BUFFER_SIZE-1-_writeIdx,format,arg_list);
    va_end(arg_list);

    if(len >= (WRITE_BUFFER_SIZE-1-_writeIdx)){
        /* 写入字符数大于缓冲区剩余数,则错误 */
        return false;
    }
    _writeIdx += len;
    LOG_INFO("request:%s",_writeBuf);
    return true;
}

/*
*功能: 按照格式写响应报文状态行，版本 状态码 短语
*参数：status：状态码；title: 短语
*/
bool HttpConn::addStatusLine(int status, const char* title){
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/*
*功能: 按照格式写响应报文首部行，版本 状态码 短语
*参数：contentLen: n
*/
bool HttpConn::addHeaders(int contentLen){
    return addContentLen(contentLen) && addLinger() && addBlankLine();
}

/*
*功能: 添加首部行中的“Content-Length"
*参数：num: 响应报文长度
*/
bool HttpConn::addContentLen(int num){
    return addResponse("Content-Length:%d\r\n",num);
}
/*
*功能: 添加首部行中的“Connection"
*/
bool HttpConn::addLinger(){
    return addResponse("Connection:%s\r\n", (_linger==true)?"keep-alive":"close");
}

/*
*功能: 添加首部行中的空行
*/
bool HttpConn::addBlankLine(){
    return addResponse("%s","\r\n");
}

/*
*功能: 添加响应报文的实体信息content
*参数：content: 信息的首地址
*/
bool HttpConn::addContent(const char* content){
    return addResponse("%s", content);
}

/*
*功能: 初始化,将数据库mysql中的用户名和密码存入_users中
*参数：sqlPool: 数据库连接池
*/
void HttpConn::initMySQLResult(SqlPool* sqlPool){
    /* 先从sql连接池中取出一个连接 */
    MYSQL* mysql = NULL;
    ConnRAII mysqlCon(&mysql, sqlPool);

    /* 在user表中检索username, password 数据， 浏览器端输入 */
    if(mysql_query(mysql, "SELECT username, passwd FROM user")){
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    /* 从表中检索完整的结果集 */
    MYSQL_RES* result = mysql_store_result(mysql);

    /* 返回结果集的列数 */
    int num_fields = mysql_num_fields(result);

    /* 返回所有字段结构的数组 */
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    /* 从结果集获取下一行，将对应的用户名和密码存入map中 */
    while(MYSQL_ROW row = mysql_fetch_row(result)){
        string name(row[0]);
        string passwd(row[1]);
        _users[name] = passwd;
    }
}