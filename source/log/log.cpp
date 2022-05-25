#include "log.h"


Log::Log(){
    _count = 0;
    _async = false;
}

/* 关闭日志文件指针 */
Log::~Log(){
    if(_fp != NULL){
        fclose(_fp);
    }
}

/*
*功能：初始化日志类，写入方式是同步还是异步
*参数：
*     -- file: 日志文件名的通用部分，前面部分则是日期，用以区分

*/
bool Log::init(const char* file, int closeLog, 
               int bufSize, int lines, int queueNum){
    /* 如果设置了阻塞队列的大小，则为异步写入 */
    if(queueNum >= 1){
        _async = true;
        /* 创建阻塞队列 */
        _logQueue = new BlockQueue<string>(queueNum);
        pthread_t tid;
        /* 创建一个写入线程，异步写日志 */
        pthread_create(&tid, NULL, flushLogThread, NULL);
    }

    _closeLog = closeLog;
    /* 输出内容的长度 */
    _bufSize = bufSize;
    _buf = new char[_bufSize];
    memset(_buf, '\0', _bufSize);
    /*日志最大行数 */
    _lineNum = lines;

    /* 根据当前时间，设置日志文件名，创建日志文件，并打开得到文件指针 */
    time_t t = time(NULL);
    struct tm* sysTm = localtime(&t);
    struct tm myTm = *sysTm;

    /* 找到'/'在 file中的位置, '/'后面则是日志文件通用名，
       然后在其前加上时间，以区分不同的日志文件 */
    const char* p = strchr(file, '/');
    char fullName[256] = {0};
    if(p == NULL){
        snprintf(fullName, 255, "%d_%02d_%02d_%s",
         myTm.tm_year+1900, myTm.tm_mon+1, myTm.tm_mday, file);
    }
    else{
        /* '/'后名字复制到_filename中 */
        strcpy(_fileName, p+1);
        /* '/'前的是路径 */
        strncpy(_dir, file, p-file+1);
        /* 完整的文件名 */
        snprintf(fullName, 255, "%s%d_%02d_%02d_%s",
          _dir, myTm.tm_year+1900, myTm.tm_mon+1, myTm.tm_mday, _fileName);
    }
    
    _today = myTm.tm_mday;
    /* 以附加的方式打开日志文件 */
    _fp = fopen(fullName, "a");
    if(_fp == NULL){
        return false;
    }
    return true;
}
    

/*
*功能：写日志的主体函数
    日志分级的实现大同小异，一般的会提供五种级别，具体的，
    -- Debug，调试代码时的输出，在系统实际运行时，一般不使用。
    -- Warn，这种警告与调试时终端的warning类似，同样是调试代码时使用。
    -- Info，报告系统当前的状态，当前执行的流程或接收的信息等。
    -- Error和Fatal，输出系统的错误信息。
    项目中给出了除Fatal外的四种分级，实际使用了Debug，Info和Error三种。
    日志写入前会判断当前day是否为创建日志的时间，行数是否超过最大行限制
    若为创建日志时间，写入日志，否则按当前时间创建新log，更新创建时间和行数
    若行数超过最大行限制，在当前日志的末尾加count/max_lines为后缀创建新log
    将系统信息格式化后输出，具体为：格式化时间 + 格式化内容
*/
void Log::writeLog(int level, const char* format, ...){
    /* 获得当前时间 */
    struct timeval now = {0,0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm* sysTm = localtime(&t);
    struct tm myTm = *sysTm;
    
    /* 日志分级：debug,warn,info,error */
    char s[16] = {0};
    switch(level){
        case 0:
        {
            strcpy(s, "[debug]:");
            break;
        }
        case 1:
        {
            strcpy(s, "[info]:");
            break;
        }
        case 2:
        {
            strcpy(s, "[warn]:");
            break;
        }
        case 3:
        {
            strcpy(s, "[error]:");
            break;
        }
        default:
        {
            strcpy(s, "[info]:");
            break;
        }
    }

    /* 查看是否需要新建日志文件 */
    _lock.lock();
    _count++;
    /* 如果当前时间和日志文件的创建时间不一致 
       或者 行数超过了最大行数,则新建日志文件;
       这里行数一直在增加，因此每多增加一个最大行数，则新建一个文件 */
    if(_today != myTm.tm_mday || _count % _lineNum == 0){
        /* 强制将文件缓冲区的数据写入文件，然后关闭旧文件 */
        fflush(_fp);
        fclose(_fp);
        
        /* 新文件名 */
        char newLog[256] = {0};
        /* 文件名中间的时间部分 */
        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", myTm.tm_year+1900, myTm.tm_mon+1, myTm.tm_mday);
        
        /* 当前时间和日志文件的创建时间不是同一天，则新建日志文件，日志文件名的发生变化 */
        if(_today != myTm.tm_mday){
            snprintf(newLog, 255, "%s%s%s", _dir, tail, _fileName);
            _today = myTm.tm_mday;
            _count = 0;
        }
        else{
            /* 行数超了,新文件名 = 原文件名._count/_lineNum */
            snprintf(newLog, 255, "%s%s%s.%lld", _dir, tail, _fileName, _count/_lineNum);
        }
        /* 打开新文件 */
        _fp = fopen(newLog, "a");
    }
    _lock.unlock();

    /* 解析输入，将format的输入传给va_list */
    va_list vaLst;
    va_start(vaLst, format);
    
    string logStr;
    _lock.lock();
    /* 写入内容格式: 时间 + 内容 */
    int n = snprintf(_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s",
                    myTm.tm_year+1900, myTm.tm_mon+1, myTm.tm_mday,
                    myTm.tm_hour, myTm.tm_min, myTm.tm_sec, now.tv_usec, s);
    /* 将传入的参数写入_buf */
    int m = vsnprintf(_buf+n, _bufSize-n-1, format, vaLst);
    /* 换行 */
    _buf[n+m] = '\n';
    /* 字符串终止符 */
    _buf[n+m+1] = '\0';
    /* 复制给string */
    logStr = _buf;
    _lock.unlock();
    
    /* 如果是异步写入，并且队列不满，则将待写入数据放入队列，等待写入 */
    if(_async && !_logQueue->full()){
        _logQueue->push(logStr);
    }
    else{
        /* 将内容写入文件 */
        _lock.lock();
        fputs(logStr.c_str(), _fp);
        _lock.unlock();
    }

    va_end(vaLst);
}

/* 强制刷新，将文件流缓冲区写入文件 */
void Log::flush(){
    _lock.lock();
    fflush(_fp);
    _lock.unlock();
}