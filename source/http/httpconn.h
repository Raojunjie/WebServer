/*
@Author    : Raojunjie
@Date      : 2022-5-14
@Detail    : http任务类
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <arpa/inet.h>
#include <string.h>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <map>
#include <mysql/mysql.h>
#include <sys/uio.h>
#include "../mysql/sqlpool.h"
#include "../locker/locker.h"
using namespace std;

/* http连接类 */
class HttpConn{
public:
   
   HttpConn(){}
   ~HttpConn(){}

   /* 主状态： 解析哪一段请求报文 */
   enum CHECK_STATE{
      REQUESTLINE = 0, HEADER, CONTENT
   };
   /* 从状态：解析某一行内容
      LINE_OK:   内容完整；
      LINE_OPEN: 内容不完整，需要继续接收；
      LINE_BAD:  内容中出现语法错误。 */
   enum LINE_STATE{
      LINE_OK = 0, LINE_BAD, LINE_OPEN
   };
   /* http请求方法 */
   enum METHOD{
      GET=0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATH
   };
   /* 解析请求报文后，http状态码 */
   /* NO_REQUEST: 请求不完整，继续读取请求的数据，跳转主线程监测读事件
      GET_REQUEST: 获得了完整的请求，执行doRequest()，响应
      BAD_REQUEST: http请求报文有语法错误，跳转processWrite()，响应
      NO_RESOURCE: 请求资源不存在，跳转processWrite()，响应
      FORBIDDEN_REQUEST: 请求资源禁止访问，跳转processWrite()，响应
      FILE_REQUEST: 请求资源可以访问，跳转processWrite()，响应
      INTERNAL_ERROR: 服务器内部错误，主状态机default时出现，一般不会出现*/
   enum HTTP_CODE{
      NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE,
      FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION
   };

   /* 文件名大小 */
   static const int FILENAME_LEN = 200;
   /* 读缓冲区大小 */
   static const int READ_BUFFER_SIZE = 2048;
   /* 写缓冲区大小 */
   static const int WRITE_BUFFER_SIZE = 1024;

   static int _epollFd;  /* epoll的文件描述符 */
   static int _userCount; /* 已连接的客户数量 */
   MYSQL* _mysql;
   int _state;  /* 本次任务的I/O事件是读还是写，0: 读；1：写 */
   int _timerFlag;   /* I/O事件处理结果，0：成功； 1：失败 */
   int _improv;   /* 0: I/O事件未被处理； 1：已被处理了 */

   void init(int sockFd, const sockaddr_in& addr, char* root, int trigMode,
   int closeLog, string user, string password, string database);
   void addFd(int epollFd, int sockFd, bool oneShot, int trigMode);
   int setNonblocking(int fd);
   void removeFd(int epollFd, int fd);
   void modFd(int epollFd, int fd, int ev, int trigMode);
   /* 返回客户端地址 */
   sockaddr_in* getAddress(){
      return &_address;
   }
   bool readOnce();
   bool write();
   void process();
   void closeConn(bool close=true);
   void initMySQLResult(SqlPool* sqlPool);

private:
   void init();
   HTTP_CODE processRead();
   bool processWrite(HTTP_CODE code);
   LINE_STATE parseLine();
   /* 获得一行内容的首地址 */
   char* getLine(){
      return _readBuf + _startLine;
   }
   HTTP_CODE parseRequestLine(char* text);
   HTTP_CODE parseHeaders(char* text);
   HTTP_CODE parseContent(char* text);
   HTTP_CODE doRequest();
   bool addResponse(const char* format, ...);
   bool addStatusLine(int status, const char* title);
   bool addHeaders(int contentLen);
   bool addContentLen(int num);
   bool addLinger();
   bool addBlankLine();
   bool addContent(const char* content);
   void unmap();
   
private:
   int _sockFd;    /* 连接后的socket */
   sockaddr_in _address;  /* 客户端地址 */
   int _trigMode;   /* epoll触发模式 */
   char* _root;     /* 资源存放的路径 */
   int _closeLog;   /* 日志文件关闭方式 */
   char _sqlUser[100];  /* sql用户名 */
   char _sqlPassword[100];  /* sql密码 */
   char _sqlDatabase[100];  /* sql库名 */

   int _bytesToSend;         /* 未发送的字节数 */
   int _bytesHaveSend;       /* 已发送的字节数 */
   CHECK_STATE _checkState; /* 报文读取的位置,请求行，报文头，内容 */
   bool _linger;   /* 是否保持长连接 */
   METHOD _method;   /* http请求方法 */
   char* _url;  /* url */
   char* _version; /* http版本 */
   char* _host; /* host */
   int _contentLen;  /* 内容长度 */
   int _cgi;   /* 是否启用POST */
   char* _content; /* 存储请求的content */

   /* 存放要发出的响应报文 */
   char _writeBuf[WRITE_BUFFER_SIZE];
   int _writeIdx;     /* 写缓冲区中字节个数 */
   /* 存放读取的请求数据 */
   char _readBuf[READ_BUFFER_SIZE];
   int _readIdx;        /* 读缓冲区中最后一个字节的下一个位置,也就是可以开始存放数据的位置 */
   int _checkedIdx;     /* 从状态机在读缓冲区中已经读取位置的下一个位置 */
   int _startLine;      /* 读缓冲区中下一行内容的首地址 */
   
   struct stat _fileStat;  /* 请求资源的状态 */
   char _realFile[FILENAME_LEN];  /* 存放响应文件的路径名 */
   char* _fileAddress;  /* 响应文件对应内存映射的首地址 */
   struct iovec _iv[2];  /*  用来整合存放响应报文 */
   int _ivCount; 

   map<string,string> _users;   /* sql中的用户 */
};



#endif