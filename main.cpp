#include "config.h"


// ./server -p 9007 -l 1 -m 0 -o 1 -s 10 -t 10 -c 1 -a 1
// 

int main(int argc, char *argv[])
{
    // 数据库信息：登录名，密码，库名
    string user = "root";
    string passwd = "rootpass";
    string databasename = "tinywebserver_ybb";

    // 命令行参数解析
    Config config;
    config.parse_arg(argc, argv);   // 获取参数中设置的端口号等信息

    WebServer server;

    // 初始化（将解析的命令行参数）
    server.init(config.Port, user, passwd, databasename, config.LogWrite, config.OptLinger, 
                config.TrigMode,  config.sql_num,  config.thread_num, config.close_log, config.actor_model);
    // 日志
    server.log_write();
    // 数据库
    server.sql_pool();
    // 线程池
    server.thread_pool();
    // 触发模式
    server.trig_mode();
    // 事件监听
    server.eventListen();
    // 运行事件循环
    server.eventLoop();

    printf("mian::return 0!\n");

    return 0;
}