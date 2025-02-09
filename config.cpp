#include "config.h"

Config::Config()
{
    Port = 9006;        // server默认监听端口号：9006
    LogWrite = 0;       // 日志写入方式，默认同步
    TrigMode = 0;       // 触发组合模式,默认listenfd LT + connfd LT
    ListenTrigMode = 0; // listenfd触发模式，默认LT
    ConnTrigMode = 0;   // connfd触发模式，默认LT
    OptLinger = 0;      // 优雅关闭链接，默认不使用
    sql_num = 8;        // 数据库连接池数量,默认8
    thread_num = 8;     // 线程池内的线程数量,默认8
    close_log = 0;      // 关闭日志,默认不关闭
    actor_model = 0;    // 并发模型,默认是proactor
}


/** argc、argv 从 main() 传递而来
./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] 
            [-t thread_num] [-c close_log] [-a actor_model]
            
./server -p 9007 -l 1 -m 0 -o 1 -s 10 -t 10 -c 1 -a 1

*/
void Config::parse_arg(int argc, char *argv[])
{
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    // 一个冒号表示p选项后必须有参数，没有参数就会报错。例如 -p argstr, 如果只有-p, 没有选项参数，报错

    // optarg：如果某个选项有参数，这包含当前选项的参数字符串
    // getopt() ： 分析命令行参数
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            Port = atoi(optarg);    // 端口号
            break;
        }
        case 'l':
        {
            LogWrite = atoi(optarg); // 日志写入方式（同步异步）
            break;
        }
        case 'm':
        {
            TrigMode = atoi(optarg); // 触发模式
            break;
        }
        case 'o':
        {
            OptLinger = atoi(optarg);   // 优雅关闭连接
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg); // 数据库连接池数量
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);    // 线程池数量
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);   // 是否关闭日志
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);  // 并发模型
            break;
        }
        default:
            break;
        }
    }
}