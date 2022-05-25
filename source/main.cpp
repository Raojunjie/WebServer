/*
@Author    : Raojunjie
@Date      : 2022-5-14
@Reference : https://github.com/qinguoyi/TinyWebServer
*/

#include "server/webserver.h"
#include <string>
using namespace std;

int main(int argc, char* argv[]){

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