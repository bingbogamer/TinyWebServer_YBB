#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"
/**
 * 如果某一用户connect()到服务器之后，长时间不交换数据，一直占用服务器端的文件描述符，导致连接资源的浪费。
 * 利用定时器把这些超时的非活动连接释放掉，关闭其占用的文件描述符。
 */

class util_timer; // 定时器

// 用户数据结构
struct client_data
{
    sockaddr_in address; // 客户端socket地址
    int sockfd;          // 占用的服务器的文件描述符
    util_timer *timer;   // 定时器
};



/* timer 定时器 —— 链表节点 */
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;          // 超时时间

    /* 返回值void，(*cb_func)表示这是函数指针，client_data *：函数 */
    /* 函数指针初始化：void (*cb_func)(client_data *) = &foo*/
    /* 调用指向的函数foo ：cb_func(&client_data) */
    /* 并没有初始化，在WebServer::timer()函数里初始化指向一个具体的函数 */
    void (*cb_func)(client_data *);

    client_data *user_data;         // 客户端数据
    util_timer *prev;               // 上节点
    util_timer *next;               // 下节点
};


// 链表 : 升序定时器, 节点类型：util_timer
// 为每个连接创建一个定时器，将其添加到链表中，并按照超时时间升序排列
class sort_timer_list
{
public:
    sort_timer_list();
    ~sort_timer_list();

    void add_timer(util_timer *timer);      // 添加定时器
    void adjust_timer(util_timer *timer);   // 调整定时器
    void del_timer(util_timer *timer);      // 删除定时器
    void tick();        // 滴答计时

private:
    void add_timer(util_timer *timer, util_timer *lst_head);    ///< 
    util_timer *head;   // 头结点指针，超时时间最小
    util_timer *tail;   // 尾节点指针，超时时间最大
};


class Utils
{
public:
    Utils(){};
    ~Utils(){};

    void init(int timelot);     // 设置m_TIMESLOT值
    
    // 对文件描述符设置为 非阻塞
    int setnonblocking(int fd);

    // 将内核事件表注册 读事件、ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIFMode);
    // 信号处理函数
    static void sig_handler(int sig);
    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);
    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    /// 静态数据成员，与类对象无关，
    static int *u_pipefd;           // 双向管道数组fd[2]指针，Utils::u_pipefd = m_pipefd;(webserver.cpp)
    static int u_epollfd;           // epoll文件描述符

    sort_timer_list m_timer_lst;    // 升序定时器链表
    int m_TIMESLOT;                 // alarm()的信号定时时间
};

/// 
void cb_func(client_data *user_data);

#endif