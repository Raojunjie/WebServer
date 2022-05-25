#include "sqlpool.h"


SqlPool::SqlPool(){
    _curConn = 0;
    _freeConn = 0;
}

SqlPool::~SqlPool(){
    destroyPool();
}

/*
*功能：单例模式，获得连接池的一个对象,全局仅此一个对象
*/
SqlPool* SqlPool::getInstance(){
    static SqlPool sqlPool;
    return &sqlPool;
}

/*
*功能：创建连接池，存放在链表中
*/
void SqlPool::init(string url, string user, string password, string database,
                   int port, int maxConn, int closeLog){
    _url = url;
    _port = port;
    _user = user;
    _password = password;
    _database = database;
    _closeLog = closeLog;

    for(int i=0; i<maxConn; ++i){
        /* 初始化一个mysql对象 */
        MYSQL* conn = NULL;
        conn = mysql_init(conn);
        if(conn == NULL){
            LOG_ERROR("MYSQL init failed");
            exit(1);
        }
        /* 建立conn到服务器本机的mysql数据库的连接 */
        conn = mysql_real_connect(conn, _url.c_str(), _user.c_str(), _password.c_str(),
               _database.c_str(), _port, NULL, 0);
        if(conn == NULL){
            LOG_ERROR("MYSQL connect failed");
            exit(1);
        }
        /* 将新建立的链接加入池中 */
        _connList.push_back(conn);
        ++_freeConn;
    }

    _reserve = Sem(_freeConn);
    _maxConn = _freeConn;
}

/*
*功能：获得一个已经连接到mysql数据库的mysql对象
*/
MYSQL* SqlPool::getConnection(){
    MYSQL* conn = NULL;
    /* 连接池中无对象 */
    if(0 == _connList.size()){
        return NULL;
    }
    /* 取出一个连接,如果没有空闲连接则阻塞 */
    _reserve.wait();
    locker.lock();
    conn = _connList.front();
    _connList.pop_front();
    _freeConn--;
    _curConn++;
    locker.unlock();
    return conn;
}

/*
*功能：释放一个连接，放回连接池中
*/
bool SqlPool::releaseConnection(MYSQL* conn){
    if(NULL == conn){
        return false;
    }

    /* 释放, 放入链表中 */
    locker.lock();
    _connList.push_back(conn);
    _freeConn++;
    _curConn--;
    locker.unlock();
    _reserve.post();
    return true;
}

/*
*功能：销毁连接池，关闭连接，释放内存
*/
void SqlPool::destroyPool(){
    locker.lock();
    if(_connList.size() > 0){
        for(auto i=_connList.begin(); i!=_connList.end(); i++){
            mysql_close(*i);
        }
        _curConn = 0;
        _freeConn = 0;
        _connList.clear();
    }
    locker.unlock();
}

/*
*功能：得到空余连接数
*/
int SqlPool::getFreeConnNum(){
    return _freeConn;
}

    

/*
*功能：从连接池中获得一个连接
*参数：
      -- sql: 传出参数，获得连接通过指针传给*sql
      -- sqlPool: 连接池
*/
ConnRAII::ConnRAII(MYSQL** sql, SqlPool* sqlPool){
    *sql = sqlPool->getConnection();
    _conn = *sql;
    _sqlPool = sqlPool;
}

/*
*功能：析构函数，释放一个连接
*/
ConnRAII::~ConnRAII(){
    _sqlPool->releaseConnection(_conn);
}