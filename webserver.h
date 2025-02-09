#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./http/http_conn.h"
#include "./threadpool/threadpool.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    // 初始化
    void init(int port, string user, string passWord, string databaseName,
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();

    // 初始化每个连接客户端用户的定时器, 并将定时器添加到定时器链表中
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclinetdata();
    bool dealwithsignal(bool &timeout, bool &stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    /* 基础*/
    int m_port;         // 服务器监听端口
    char *m_root;       // root 文件夹（工作目录）绝对路径
    int m_log_write;
    int m_close_log;    // 关闭日志
    int m_actormodel;   //  1 reactor  0 proactor

    int m_pipefd[2];  // 双向管道，调用socketpair()进行初始化
    int m_epollfd;    // 指定的内核事件表
    http_conn *users; // 所有连接用户

    /* 线程池 */
    ThreadPool<http_conn> *m_pool;
    int m_thread_num;

    /* 数据库 */ 
    ConnectionPool *m_connPool;
    string m_user;         // 登陆数据库用户名
    string m_passWord;     // 登陆数据库密码
    string m_databaseName; // 使用数据库名
    int m_sql_num;

    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];


    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;         // 触发模式  LT  ET
    int m_LISTENTrigmode;   // 监听触发模式  0：只接受一次客户端连接， 1：循环接受客户端连接，直到accept失败 或 连接数量超过最大
    int m_CONNTrigmode;     // 连接触发模式

    /* 定时器 */
    client_data *users_timer;
    Utils utils;        /* 包含升序定时器链表 */
};

#endif