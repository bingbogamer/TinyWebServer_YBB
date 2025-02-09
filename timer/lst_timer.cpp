#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_list::sort_timer_list()
{
    head = NULL;
    tail = NULL;
}

// 释放所有的链表节点
sort_timer_list::~sort_timer_list()
{
    util_timer *temp = head;
    while (temp)
    {
        head = temp->next;
        delete temp;
        temp = head;
    }
}

//
void sort_timer_list::add_timer(util_timer *timer)
{
    // 先判断传入的定时器，是否是空节点
    if (!timer)
    {
        return;
    }
    // 链表头节点为NULL，即当前链表为空
    if (!head)
    {
        head = tail = timer;        ///< 
        return;
    }
    // 添加timer超时时间 < head超时时间（head超时时间最小，即最临近超时）
    if (timer->expire < head->expire)
    {
        // 将当前timer插入到链表的头部
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    // 添加timer超时时间 > head定时器的超时时间，则将
    add_timer(timer, head);
}

// 定时器expire排列： head的
void sort_timer_list::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;   // prev ：保存当前链表的头结点
    util_timer *temp = prev->next; // 当前链表的头结点
    while (temp)
    {
        // 如果timer超时时间 < 下一个定时器的超时时间
        if (timer->expire < temp->expire)
        {
            // 将当前定时器 插入到 prev定时器之后
            prev->next = timer; // prev定时器的下一个定时器 timer
            timer->next = temp; //
            temp->prev = timer;
            timer->prev = prev;
            break;
        }
        // prev、temp都向后移动
        prev = temp;
        temp = temp->next;
    }
    // 如果下一个定时器是NULL，即prev指向了最后一个定时器
    if (!temp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

// 调整定时器
void sort_timer_list::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    util_timer *temp = timer->next;
    // 传入timer超时时间 < 传入定时器的下一个定时器的超时时间
    if (!temp || (timer->expire < temp->expire))
    {
        return;
    }
    // timer定时器是链表头结点
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

// 删除定时器
void sort_timer_list::del_timer(util_timer *timer)
{
    // 先判断传入的对象是否为空
    if (!timer)
    {
        return;
    }
    // 删除定时器是 链表中唯一的1个节点（头节点 && 尾节点）
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    // 删除头结点
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    // 删除尾节点
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    // 删除的不是头结点、尾节点
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

// 遍历定时器链表，删除超时的定时器
void sort_timer_list::tick()
{
    // 当前为空链表，则直接返回
    if (!head)
    {
        return;
    }
    // 记录当前时间
    time_t cur = time(NULL);
    util_timer *temp = head;            // temp保存头节点
    // 循环遍历  定时器链表 ，查看链表中的定时器有没有超时
    while (temp)
    {
        // （定时器还没有超时）当前时间 < 当前定时器的超时时间
        if (cur < temp->expire)
        {
            break;
        }
        // 定时器超时，执行回调函数
        // 传入当前链接的客户端数据，将该连接 删除
        temp->cb_func(temp->user_data);
        // 头结点指针指向下一个
        head = temp->next;
        if (head)
        {
            head->prev = NULL;
        }
        // 删除该定时器
        delete temp;
        temp = head;
    }
}

// 时间槽？
void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

// 设置文件描述符为 非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表 注册的读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    // 触发模式
    if (1 == TRIGMode)
    {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // ET模式
    }
    else
    {
        event.events = EPOLLIN | EPOLLRDHUP; // LT模式
    }
    // 
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}


/**
 * 信号处理函数，使用管道将信号“传递”给住循环
 * 信号处理函数：往管道的写端
*/
void Utils::sig_handler(int sig)
{
    // 为了保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    // 专门用于socket数据发送的系统调用
    send(u_pipefd[1], (char *)&msg, 1, 0);      // 向管道文件发送1个信号，来触发信号处理函数
    errno = save_errno;
    // 也就是会所，send函数失败会覆盖errno，所以这里保存errno
}

// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler; // 设置信号处理函数
    if (restart)    
    {
        sa.sa_flags |= SA_RESTART; //  重新调用被该信号终止的系统调用
    }
    
    sigfillset(&sa.sa_mask);    /* 在信号集中设置所有信号 */
    int ret = sigaction(sig, &sa, NULL); /* 设置信号处理函数 */
    assert(ret != -1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    // 定时处理任务，实际上就是调用tick()
    m_timer_lst.tick();                 ///< 遍历定时器链表，删除超时的定时器
    
    // 因为一次alarm调用 只会引起一次SIGALRM信号，所以要重新定时，以不断触发SIGALRM信号
    alarm(m_TIMESLOT);          ///< 在m_TIMESLOT秒后，发送SIGALRM信号
}

/* 向客户端连接fd发送  字符串信息，并关闭与客户端连接*/
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;

/* 将客户端sockfd从epoll上删除,关闭连接，连接用户数量-1 */
void cb_func(client_data *user_data)
{
    /* 将客户端sockfd从epoll上删除 */
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);              /* 断言：判断用户数据指针是否为空 */
    close(user_data->sockfd);   /* 关闭客户端连接 */

    http_conn::m_user_count--;      /* 连接用户数量-1*/
}
