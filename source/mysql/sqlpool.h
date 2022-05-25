/*
@Author    : Raojunjie
@Date      : 2022-5-21
@Detail    : sql连接池类和sql连接类
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#ifndef SQLPOOL_H
#define SQLPOOL_H

#include <mysql/mysql.h>
#include <list>
#include <string>

#include "../log/log.h"
#include "../locker/locker.h"
using namespace std;

/* 连接池类,创建一些sql连接供使用 */
class SqlPool{
public:
    MYSQL* getConnection();
    bool releaseConnection(MYSQL* conn);
    int getFreeConnNum();
    void destroyPool();
    void init(string url, string user, string password, string database,
             int port, int maxConn, int closeLog);

    static SqlPool* getInstance();

private:
    SqlPool();
    ~SqlPool();

    int _maxConn;  /* 最大连接数 */
    int _curConn;  /* 当前已使用连接数 */
    int _freeConn; /* 空闲连接数 */
    list<MYSQL*> _connList;  /* 连接池 */
    Locker locker;   /* 用于多线程保护连接池 */
    Sem _reserve;   /* 信号量，表示空闲连接数 */

public:
    string _url;   /* 主机地址 */
    int _port;  /* 数据库端口号 */
    string _user;  /* 数据库用户名 */
    string _password; /* 用户密码 */
    string _database; /* 数据库名 */
    int _closeLog;  /* 日志开关 */
};

/* sql连接类，可以自动释放资源 */
class ConnRAII{
public:
    ConnRAII(MYSQL** conn, SqlPool* sqlPool);
    ~ConnRAII();

private:
    MYSQL* _conn;
    SqlPool* _sqlPool;
};

#endif