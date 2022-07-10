/*
@Author    : Raojunjie
@Date      : 2022-5-14
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#include "server/webserver.h"
#include <string>
#include <iostream>
using namespace std;

bool daemonize(){
    /* 创建子进程， 退出父进程， 使程序在后台进行 */
    pid_t pid = fork();
    if(pid < 0){
        return false;
    }
    else if(pid > 0){
        exit(0);
    }

    /* 设置文件掩码, 掩码为0， 所有文件的权限都是777 */
    umask(0);
    
    /* 创建新的会话，脱离原来的会话和控制终端 */
    pid_t sid = setsid();
    if(sid < 0){
        return false;
    }

    /* 创建新的子进程，结束成为会话组长的子进程，这样进程无法重新打开新的控制终端 */
    pid = fork();
    if(pid < 0){
        return false;
    }
    else if(pid > 0){
        exit(0);
    }

    /* 改变工作目录 */
    //chdir("/home/raojunjie/work/WebServer");

    /* 重定向输入输出 */
    int fdin = open("/dev/null", O_RDONLY);
    int fdout = open("/home/raojunjie/work/WebServer/stdout", O_RDWR | O_CREAT,0666);
    int fderr = open("/home/raojunjie/work/WebServer/stderr", O_RDWR | O_CREAT,0666);
    dup2(fdin, 0);
    if(dup2(fdout, 1) == -1){
        printf("重定向失败\n");
        exit(-1);
    }
    dup2(fderr, 2);

    return true;
}
int main(int argc, char* argv[]){

    if(daemonize() == false){
        cout<<" 创建进程失败。"<<endl;
        exit(1);
    }
    
    /* MySQL的信息：登录名、密码和库名 */
    string user = "debian-sys-maint";
    string password = "nTRqwzyiIU6IpDkJ";
    string database = "yourdb";

    /* 设置服务器参数 */
    WebServer server;
    server.setSql(user, password, database);
    server.parseArgs(argc, argv);

    server.logWrite();
    server.sqlPool();
    server.threadPool();
    server.trigMode();
    server.eventListen();
    server.eventLoop();

    return 0;
}