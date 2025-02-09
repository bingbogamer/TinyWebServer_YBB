#include "webserver.h"

WebServer::WebServer()
{
    // http_conn类对象
    users = new http_conn[MAX_FD]; // 动态内存 —— 创建http_conn数组

    // m_root ： root 文件夹绝对路径
    char server_path[200];
    getcwd(server_path, 200); // 将当前工作目录的绝对路径复制到参数buffer所指的内存空间

    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1); // 申请动态内存，保存服务器路径 server_path
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // 定时器
    users_timer = new client_data[MAX_FD]; // 用户数据 数组65536
}

WebServer::~WebServer()
{
    close(m_epollfd);  // 关闭内核事件表 文件描述符
    close(m_listenfd); //
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete[] users;       // 释放所有 http_conn *users
    delete[] users_timer; // 释放动态内存中的 用户数据
    delete m_pool;        // 释放动态内存中的 数据库连接池对象
}

/* 根据main函数中解析的命令行参数，初始化WebServer */
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;                 // 端口号
    m_user = user;                 // 登陆数据库用户名
    m_passWord = passWord;         // 数据库密码
    m_databaseName = databaseName; // 数据库名称
    m_sql_num = sql_num;           // 数据库连接池数量
    m_thread_num = thread_num;     // 线程池数量
    m_log_write = log_write;       //
    m_OPT_LINGER = opt_linger;     // 优雅关闭
    m_TRIGMode = trigmode;         // 触发模式  ET  LT？
    m_close_log = close_log;       // 日志开启？
    m_actormodel = actor_model;    //
}


/**
 * m_LISTENTrigmode: 
 * m_CONNTrigmode: 传递给 http_conn::init，控制 http_conn 中的 数据读写是 ET模式 还是 LT模式
*/
void WebServer::trig_mode()
{
    // LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0; // listen 触发: 0:只接受
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        // 初始化日志
        if (1 == m_log_write)
        {
            /* 异步日志： 将所写的日志内容先存入阻塞队列，写线程从阻塞队列中取出内容，写入日志 */
            // 异步需要设置阻塞队列的长度，同步不需要设置
            // bool Log::init(const char *file_name,  close_log,  log_buf_size,  split_lines,  max_queue_size)
            Log::getInstance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        }
        else
        {
            /* 同步日志 ：在工作线程中写入日志 */
            Log::getInstance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
        }
    }
}

// SQL数据库池
void WebServer::sql_pool()
{
    // 初始化数据库连接池
    m_connPool = ConnectionPool::getInstance();
    // 127.0.0.1    localhost
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);
    // 初始化数据库读取表
    users->initmysql_result(m_connPool);
}

// 线程池
void WebServer::thread_pool()
{
    m_pool = new ThreadPool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

// 事件监听
void WebServer::eventListen()
{
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0); // 创建 监听文件描述符
    assert(m_listenfd >= 0);

    // 优雅关闭连接(若有数据待发送，则延迟关闭)
    if (0 == m_OPT_LINGER)
    {
        struct linger temp = {0, 1}; /* l_onoff = 0, 关闭linger，默认行为：将TCP发送缓冲区的残留数据发送 */
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &temp, sizeof(temp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger temp = {1, 1}; /* l_onoff = 1 */
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &temp, sizeof(temp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port); /* 监听端口号*/

    int reuse = 1;
    /* 强制使用被处于 TIME_WAIT状态的 连接占用的socket地址
    即使socket处于 TIME_WAIT状态，与之绑定的socket地址（IP + port） 也可以立即被重用*/
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);

    
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT); // 最小超时单位

    // epoll 创建内核事件表 文件描述符
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd; /*将默认的-1值 该为现在的m_epollfd */

    /* 创建2个相互连接的管道套接字（双向管道，）*/
    /* pipe():创建的描述符一端只能用于读，一端用于写，socketpair()创建的描述符任意一端既可以读也可以写*/
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);

    utils.setnonblocking(m_pipefd[1]);             /* 设置为非阻塞*/
    utils.addfd(m_epollfd, m_pipefd[0], false, 0); /* 将m_pipefd[0]添加到epoll监听，并设置非阻塞*/

    /* 信号设置 */
    /* 设置信号处理函数 */
    /* SIGPIPE:  往读端已关闭的 管道、socket连接 写数据，进程会收到信号SIGPIPE，导致进程异常终止*/
    utils.addsig(SIGPIPE, SIG_IGN);                  /* 忽略目标信号*/
    utils.addsig(SIGALRM, utils.sig_handler, false); /* 定时器信号 */
    utils.addsig(SIGTERM, utils.sig_handler, false); /* 终止进程信号，kill命令默认发送的就是该信号 */

    alarm(TIMESLOT); /*定时*/

    // 工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

// 初始化每个连接客户端用户的定时器, 并将定时器添加到定时器链表中
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    /* 初始化连接 */
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    /* 初始化client_data 数据*/
    /* 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中*/
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    util_timer *timer = new util_timer;      /* 定时器 —— 链表节点 */
    timer->user_data = &users_timer[connfd]; // 客户端数据
    
    /* 初始化定时器的函数指针 为 cb_func*/
    timer->cb_func = cb_func;

    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

/* 若有数据传输，则将定时器往后延迟3个单位, 并对新的定时器在链表上的位置进行调整*/
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;

    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

/** 处理指定的sockfd 定时器
 * 执行定时器回调函数，即将客户端sockfd从epoll上删除,关闭连接，连接用户数量-1
 * 关闭连接后，将定时器从 升序定时器链表 中删除，并delete释放该定时器
 */
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    /* util_timer : 定时器链表节点 */
    /* 执行cb_func函数指针 指向的 回调函数cb_func：将客户端sockfd从epoll上删除,关闭连接，连接用户数量-1*/
    timer->cb_func(&users_timer[sockfd]);

    /* 关闭连接后，将定时器从 升序定时器链表 中删除，并delete释放该定时器*/
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

/* 处理 客户端数据 */
bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    // 监听触发模式  LT模式
    // 0：只接受一次客户端连接
    if (0 == m_LISTENTrigmode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        // accept失败，返回-1，并设置errno
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }

        // accept成功
        // 1. 用户数量超过最大连接数量，则向客户端的发送信息，并关闭连接
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        // 2. 没有异常，初始化的客户端定时器
        timer(connfd, client_address);
    }
    else    // 监听触发模式  ET模式
    {
        /* 1：循环接受客户端连接，直到accept失败 或 连接数量超过最大 */
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            // accept失败，返回-1，并设置errno
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            // accept成功
            // 1. 用户数量超过最大连接数量，则向客户端的发送信息，并关闭连接
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            // 没有异常，则设置定时器
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

/**
 * 信号处理函数
 * 返回 是否超时，是否停止服务
 */
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];

    /* 从 双向管道读端 m_pipefd[0]上读取数据，存储到signals缓冲区 */
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);

    // 出错
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0) // 对方已经关闭
    {
        return false;
    }
    else // 正常，则读取ret个数据
    {
        /* 因为每个信号值占一个字节，所以按字节来逐个接收信号。*/
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
                timeout = true; /* 定时器到时信号，则超时*/
                break;
            case SIGTERM:
                stop_server = true; /*kill命令信号 */
                break;
            }
        }
    }
    return true;
}

/* 读事件，包括reactor 和 proactor模式*/
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer; /*选取sockfd对应的定时器*/

    // 1:reactor模式
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        // 若监测到 读事件，将该事件放入请求队列，让工作线程竞争处理任务
        /* users是动态数组头指针，*/
        m_pool->append(users + sockfd, 0);

        /* 主线程 一直等待该http连接的请求处理完毕(improv被置1)，如果请求处理失败则关闭该http连接。*/
        while (true)
        {
            /* 工作线程已经处理读写请求 */
            if (1 == users[sockfd].improv)
            {
                /* 1： 工作线程 读写数据出错 */
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd); /* 断开用户的连接, 并从定时器链表中删除对应timer定时器*/
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else // 2:proactor模式
    {
        /* 读成功*/
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 若监测到读事件，将该事件放入请求队列，让工作线程竞争处理任务
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else /* 读失败*/
        {
            deal_timer(timer, sockfd); /* 断开用户的连接, 并从定时器链表中删除对应timer定时器*/
        }
    }
}

/* 写事件，*/
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1); /*往请求队列添加写任务， 1:写*/

        while (true)
        {
            /* 工作线程已经处理写请求 */
            if (1 == users[sockfd].improv)
            {
                /* 1： 工作线程 写出错 */
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else // 2:proactor模式
    {
        /* 写成功*/
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else /* 写失败*/
        {
            deal_timer(timer, sockfd); /* 断开用户的连接, 并从定时器链表中删除对应timer定时器*/
        }
    }
}

/* 事件循环 */
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;
    printf("WebServer::eventLoop start!\n");

    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        /* 遍历所有就绪事件*/
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            // 处理新客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag){
                    LOG_ERROR("%s", "dealwithsignal failure");
                }
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }

        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}
