#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"

using namespace std;

class Config
{
public:
    Config();   /* 构造函数中 对数据成员初始化 */
    ~Config(){};

    void parse_arg(int argc, char *argv[]); 
    int Port;           // 端口号
    int LogWrite;       // 日志写入方式（同步异步）
    int TrigMode;       // 触发模式
    int ListenTrigMode; // listenfd触发模式
    int ConnTrigMode;   // connfd触发模式
    int OptLinger;      // 优雅关闭连接
    int sql_num;        // 数据库连接池数量
    int thread_num;     // 线程池数量
    int close_log;      // 是否关闭日志
    int actor_model;    // 并发模型
};

#endif // ! CONFIG_H
