/*
@Author    : Raojunjie
@Date      : 2022-5-23
@Detail    : 日志类
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#ifndef LOG_H
#define LOG_H

#include <string.h>
#include <string>
#include <stdlib.h>
#include <stdarg.h>
#include "blockqueue.h"
using namespace std;

/* 日志类 */
class Log{
public: 
    /* 单例模式，C++11后静态变量具有线程安全特性 */
    static Log* getInstance(){
        static Log instance;
        return &instance;
    }
    static void* flushLogThread(void* args){
        Log::getInstance()->asyncWriteLog();
        return NULL;
    }
    bool init(const char* file, int closeLog, int bufSize = 8192, int lines = 5000000, int queueNum = 0);
    void writeLog(int level, const char* format, ...);
    void flush(void);

private:
    Log();
    virtual ~Log();
    /* 异步写入的调用函数，写一条日志到日志文件 */
    void asyncWriteLog(){
        string singleLog;
        /* 从阻塞队列中取出一个日志string，写入文件，如果没有则阻塞 */
        while(_logQueue->pop(singleLog)){
            _lock.lock();
            fputs(singleLog.c_str(), _fp);
            _lock.unlock();
        }
    }

private:
    char _dir[128]; /* 文件路径 */
    char _fileName[128]; /* 日志文件名 */
    int _lineNum;   /* 日志文件最大行数 */
    int _bufSize;   /* 日志缓冲区大小 */
    long long _count; /* 日志行数 */
    int _today; /* 当前时间，单位：天 */
    FILE* _fp;   /* 日志文件的文件指针 */
    char* _buf;  /* 日志缓冲区 */
    BlockQueue<string>* _logQueue;   /* 阻塞队列，存放待写入的日志内容 */
    bool _async;  /* 同步还是异步写入，TRUE：异步 */
    Locker _lock;
    int _closeLog; /* 是否关闭日志功能,0:不关闭；1：关闭 */
};

/* 供外界调用的日志写入函数 */
#define LOG_DEBUG(format, ...) if(0 == _closeLog) {Log::getInstance()->writeLog(0, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_INFO(format, ...) if(0 == _closeLog) {Log::getInstance()->writeLog(1, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_WARN(format, ...) if(0 == _closeLog) {Log::getInstance()->writeLog(2, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_ERROR(format, ...) if(0 == _closeLog) {Log::getInstance()->writeLog(3, format, ##__VA_ARGS__); Log::getInstance()->flush();}

#endif