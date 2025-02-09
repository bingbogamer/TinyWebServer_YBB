#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

/* 定义HTTP响应的一些状态信息 */
const char *ok_200_title = "OK";
/* */
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

/* 全局变量 */
MutexLocker m_lock;
map<string, string> users_map;  /* 保存所有用户名和密码 */

// /* 网站的根目录 */
// const char *doc_root = "/var/www/html";


/* 初始化数据库读取表 */
void http_conn::initmysql_result(ConnectionPool *connPool)
{
    // 先取出1个连接（数据库连接池）
    MYSQL *mysql = NULL;
    ConnectionRAII mysqlcon(&mysql, connPool);

    // 在数据库的user表中，检索username, passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {   
        LOG_ERROR("SELECT Error:%s\n", mysql_error(mysql));
    }
    // 从表中检索 完整的结果集
    MYSQL_RES* result = mysql_store_result(mysql);

    // 返回结果集中的列数(字段数)
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将所有用户对应的用户名和密码，存入users_map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);   /*username*/
        string temp2(row[1]);   /*password*/
        users_map[temp1] = temp2;
    }
    
}

/* 将文件描述符设置成 非阻塞 */
int setNonBlocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}


/** 将内核事件表epollfd 注册 fd， 监听 可读 | TCP连接被对方关闭 事件，将 fd 设为非阻塞
 * TRIGMode ：1 ET ； 0 LT
 * one_shot ：1 EPOLLONESHOT ； 0： 
 *      fd最多触发1次注册的事件(直到重置epoll_ctl重新注册EPOLLONESHOT事件)
 *      避免一个线程在处理socket上数据时，新到来的数据唤醒另一个线程来读取数据，出现两个线程同时操作一个socket的局面
*/
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd); 
}
 

/* 将文件描述符fd 从 epollfd 移除，并关闭fd */
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}


/** 将事件重置为 EPOLLONESHOT
 * 注册了EPOLLONESHOT事件的socket一旦被某个线程处理完毕，该线程应该立即重置EPOLLONESHOT，
 * 否则socket下一次可读时，不会触发 EPOLLIN 事件
 * 
 * TRIGMode ：1 ET ； 0 LT
*/
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
        
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}


/*类静态数据成员，必须在类外部定义和初始化*/
int http_conn::m_user_count = 0; /* 统计用户数量 */
int http_conn::m_epollfd = -1;


/* 关闭1个连接，客户总数-1 */
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;      /* connfd = accept() */
        m_user_count--;     /* 关闭一个连接时，将客户数总量-1 */
    }
}


/** 初始化新接受的连接
 * 在WebServer::timer() 被调用
 * root : 传入的 root 网页资源文件夹 的服务器绝对路径
*/
void http_conn::init(int connfd, const sockaddr_in &client_address, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = connfd;  /* 发起连接的客户端socket */
    m_address = client_address;

    /* 地址复用，避免TIME_WAIT状态，仅用于调试，实际使用时应该去掉 */
    // int reuse = 1;
    // setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* 注册fd， 监听 可读 + 对方关闭TCP连接 事件，设为非阻塞 + EPOLLONESHOT */
    addfd(m_epollfd, connfd, true, m_TRIGMode);
    m_user_count++;

    //当浏览器出现连接重置时，可能：网站根目录出错、http响应格式出错、访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

/* 初始化连接 */
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE; // 正在分析请求行
    m_linger = false;                        /* 短连接 */

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    cgi = 0;
    m_state = 0;
    timer_flag = 0;  /* 0：定时器已删除，解绑客户端连接 1:定时器正绑定客户端连接*/
    improv = 0;


    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}


/**从状态机：用于解析出一行内容 
 * 从状态机: 将每一行的末尾\r\n 改为\0\0，以便于主状态机直接取出对应字符串进行处理
 */
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    /**
     * m_checked_idx 指向读缓冲区m_read_buf中当前正在分析的字节
     * m_read_buf中第 0~m_checked_idx 字节都已经分析完毕
     * m_read_idx ：指向m_read_buf中客户数据的最后一个字节的下一个位置
     * 第 m_checked_idx ~ m_read_idx - 1 字节由下面的循环依次解析
     */
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        /* 获得当前要分析的字符 */
        temp = m_read_buf[m_checked_idx];

        /* 如果当前字节是'\r'回车符，说明可能读取到一个完整的行 */
        if (temp == '\r')
        {
            /** 如果'\r'字符碰巧是目前buffer中已读数据的最后一个字节，那么这次分析没有读取到一个完整的行
             * 返回LINE_OPEN表示还需要继续读取客户数据才能进一步分析
             */
            if ((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            /* 如果没有分析到最后一个字符，并且下一个字符是'\n' 换行符，则说明成功读取到一个完整的行 */
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            /* 否则，说明客户发送的HTTP请求存在语法问题 */
            return LINE_BAD;
        }
        /* 如果当前的字节是\n，可能读取到一个完整的行 */
        else if (temp == '\n')
        {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r'))
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;    // TODO:
        }
    }
    /* 如果所有内容分析完毕，也没有遇到\r字符，则返回LINE_OPEN，表示还需要继续读取客户数据才能进一步分析 */
    return LINE_OPEN;
}


/* 循环读取客户数据，直到无数据可读 or 对方关闭连接 */
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    // 读取的数据长度，超过了读缓冲区的长度
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    int bytes_read = 0;

    // LT(读取数据，数据没有读取，下次还会触发事件)
    if (0 == m_TRIGMode)
    {
        // 将数据读取到 m_read_buf + m_read_idx 开始的地址， 期望读取长度为READ_BUFFER_SIZE - m_read_idx
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;
        if (bytes_read <= 0)
        {
            return false;
        }
        return true;
    }
    else // ET模式（循环读取数据，以确保socket读缓存中的所有数据读出）
    {
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                /* 非阻塞IO，EAGAIN和EWOULDBLOCK 表示数据已经全部读取完毕，*/
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                /* 否则，recv出错*/
                return false;
            }
            else if (bytes_read == 0)   /* 对方关闭连接 */
            {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    } 
}


/* 解析请求行，获得请求方法、目标url、HTTP版本号 */
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // " \t"：空格+制表符，查找text中第一次出现 空格 or 制表符 的字符位置，如果未找到字符则返回 NULL
    m_url = strpbrk(text, " \t");

    /* 如果请求行中没有 空格 或 '\t'字符，则HTTP请求必有问题 */
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    // 将空格值 写 '\0', m_url 指向空格后url的位置
    *m_url++ = '\0';    

    char *method = text;

    // strcasecmp ： 判断字符串是否相等，忽略大小写, 遇到'\0'为止
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if(strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;    /* 是否启用POST */
    }
    else
        return BAD_REQUEST; /* 错误请求方法 */

    // strspn ： 返回m_url开始出现 空格、\t的字符数
    // 跳过前面空格，指向URL
    m_url += strspn(m_url, " \t");

    // 查找text中第一次出现 空格 or 制表符 的字符位置
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;


    // 将空格值 写 '\0', m_version 指向空格后的位置
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    /* 仅支持HTTP/1.1 */
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    
    /* 检查URL是否合法 */
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        // 在m_url字符串中 查找 字符’/’ 第一次出现的位置
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    
    // 当 m_url == NULL 或 
    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }
    printf("The request URL is: %s\n", m_url);

    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
        
    /* HTTP请求行处理完毕，状态转移到头部字段分析 */
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/* 解析HTTP请求的 首部行 */
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    /* 遇到一个空行，表示首部行 解析完毕 */
    if (text[0] == '\0')
    {
        /* 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，状态机转移到CHECK_STATE_CONTENT 状态 */
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        /* 否则：已经得到了一个完整的HTTP请求 */
        return GET_REQUEST;
    }
    /* Connection */
    else if (strncasecmp(text, "Connetction:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        // 
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    /* Content-Length */
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    /* Host */
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    /* 其他首部行都不处理 */
    else
    {
        LOG_INFO("oop! unknow header %s\n", text);
    }
    return NO_REQUEST;
}


/* 判断http请求是否被完整读入 (没有真正解析HTTP请求的消息体) */
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    // 消息体长度 + 已检查索引 <= 读缓冲区中已经读入数据 索引
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;    /* 存储请求头数据 */
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


/**主状态机： 解析报文
 * 整体流程：通过while循环，将主从状态机进行封装，对报文的每一行进行循环处理。
 * 如果请求读取完整，调用do_request()执行请求，将相应的文件映射到内存准备写
*/
http_conn::HTTP_CODE http_conn::process_read()
{
    // 初始化 从状态机的行读取状态 LINE_OK、HTTP请求解析结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST; 
    char *text = 0;

    // 1. 正在分析请求数据 && 读取到一个完整的行
    // 2. 读取到一个完整的行
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || 
            ((line_status = parse_line()) == LINE_OK))
    {
        // text = m_read_buf + m_start_line
        // parse_line 已经将 \r\n 替换为 \0\0
        text = get_line();
        
        // 更新 m_start_line 为下一行在m_read_buf的起始位置
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        printf("got 1 http line: %s\n", text);

        /* m_check_state : 记录主状态机当前所处的状态 */
        switch (m_check_state)
        {
        /* 分析请求行  */
        case CHECK_STATE_REQUESTLINE: 
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;

        /* 分析首部行 */
        case CHECK_STATE_HEADER:
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        /* 分析请求数据 */
        case CHECK_STATE_CONTENT:
            ret = parse_content(text);
            if (ret == GET_REQUEST)
            {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}


/** 当得到一个完整、正确的HTTP请求时，就分析目标文件的属性。
 * 如果目标文件存在、对所有用户可读，且不是目录，则是用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
 */
http_conn::HTTP_CODE http_conn::do_request()
{
    // const char *doc_root = "/var/www/html";     /* 网站的根目录 */
    // char *m_url;                    /* 客户请求的目标文件的文件名 */
    // char m_real_file[FILENAME_LEN]; /* 客户请求的目标文件的完整路径，其内容等于doc_root + m_url, */
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');

    //处理cgi (启用POST)
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        // 注册
        // /3  CGISQL.cgi
        //  POST请求，进行注册校验
        //  注册成功跳转到log.html，即登录页面
        //  注册失败跳转到registerError.html，即注册失败页面
        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            // "INSERT INTO user(username, passwd) VALUES(‘name’, ‘password')
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // 未发现重名用户
            if (users_map.find(name) == users_map.end())
            {
                m_lock.lock();
                // 向数据库中添加 用户名、密码
                int res = mysql_query(mysql, sql_insert);
                users_map.insert(pair<string, string>(name, password));
                m_lock.unlock();

                // 成功：返回0  错误：返回非0值
                if (!res)
                    strcpy(m_url, "/log.html"); // 登录界面
                else
                    strcpy(m_url, "/registerError.html"); // 注册错误
            }
            // 已有重名用户
            else
                strcpy(m_url, "/registerError.html");
        }
        // 登录，直接判断
        // 若浏览器端输入的用户名和密码在users_map中可以查找到，返回1，否则返回0
        // ●/2  CGISQL.cgi
        //  POST请求，进行登录校验
        //  验证成功跳转到welcome.html，即资源请求成功页面
        //  验证失败跳转到logError.html，即登录失败页面
        else if (*(p + 1) == '2')
        {
            if (users_map.find(name) != users_map.end() && users_map[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    // /0 : POST请求，跳转到 register.html , 注册页面
    if (*(p + 1) == '0')
    {
        // 为什么要用动态数组，而不用局部数组 char m_url_real[] = "/register.html" 呢？
        // 使用 malloc 可以避免在栈上分配大数组，这可能会影响程序的性能
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // /1 : POST请求，跳转到log.html，登录页面
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // /5 : POST请求，跳转到picture.html，即图片请求页面
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // /6 : POST请求，跳转到video.html，即视频请求页面
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // /6 : POST请求，跳转到video.html，即视频请求页面
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 获取文件的信息结构
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    // 文件访问权限：其他读 不允许，返回没有访问权限
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    // 文件类型是目录，返回请求错误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    /* 存储映射 */
    int fd = open(m_real_file, O_RDONLY);
    if (fd == -1) {
        // 打开文件失败
        printf("open error---------------------------\n");
    }
    printf("open success: %s\n", *m_real_file);

    // PROT_READ ： 映射区的保护要求，只读打开
    // MAP_PRIVATE ： 私有映射，对存储区的修改只会修改文件副本，不影响源文件
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

/* 对内存映射区执行munmap操作 */
void http_conn::unmap()
{
    if (m_file_address)
    {
        // 解除映射
        munmap(m_file_address, m_file_stat.st_size);
        // 指针置空
        m_file_address = 0;
    }
}


/** 将HTTP响应报文从写缓冲区发送给浏览器
 * server子线程 : 调用process_write() 完成响应报文，随后注册epollout事件强制触发写事件。
 * server主线程 : 检测写事件，并调用http_conn::write() 将响应报文发送给浏览器端
*/
bool http_conn::write()
{
    int temp = 0;

    // 若要发送的数据长度为0
    // 表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);    // 将m_sockfd 重置为 EPOLLONESHOT | EPOLLRDHUP
        init();
        return true;
    }
 
    while (1)
    {
        /*writev:集中写 —— 将分散的内存数据 一并写入文件描述符中*/
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0)
        {
            /** 如果TCP写缓存没有空间，则等待下一轮的EPOLLOPUT事件。
             * 虽然在此期间，服务器无法立即接收到同一客户的下一个请求，但这可以保证连接的完整性  */
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }


        if (bytes_to_send <= 0)
        {
            /* 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接 */
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}


/* 往写缓冲 m_write_buf 中写入待发送的数据 */
bool http_conn::add_response(const char *format, ...)
{
    //如果写入内容超出m_write_buf大小则报错
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;

    va_list arg_list;
    // 初始化 arg_list 指向 可变参数的第一个参数
    va_start(arg_list, format);

    // 将数据 format 从可变参数列表写入 写缓冲区，返回写入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);

    // 如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }

    // 更新m_write_idx位置
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}


// 添加状态行
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}


// 添加消息报头，具体的添加文本长度、连接状态和空行
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() && add_blank_line();
}


// 添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-length: %d\r\n", content_len);
}

// 添加文本类型，这里是html
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}


//添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

//添加空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

// 添加文本content
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}


/** 将HTTP响应报文从写缓冲区发送给浏览器
 * server子线程 : 调用process_write() 完成响应报文，随后注册epollout事件强制触发写事件。
 * server主线程 : 检测写事件，并调用http_conn::write() 将响应报文发送给浏览器端
*/
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        // 往写缓冲 m_write_buf 中依次写入 状态行、添加消息报头（添加文本长度、连接状态和空行）
        // 
        add_status_line(500, error_500_title); 
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false; 
        break;
    }
    case BAD_REQUEST:
    {
    //     add_status_line(400, error_400_title);
    //     add_headers(strlen(error_400_form));
    //     if (!add_content(error_400_form))
    //         return false;
    //     break;
    // }
    // case NO_RESOURCE:
    // {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);

        // 请求文件（html）的大小
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}


/* 由线程池中的 工作线程调用，这是处理HTTP请求的入口函数 */
void http_conn::process()
{
    // 解析客户端 请求报文
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }

    // 返回给客户端
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
