#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h> // IP地址转换
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
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;       /* 文件名的最大长度 */
    static const int READ_BUFFER_SIZE = 2048;  /* 读缓冲区的大小 */
    static const int WRITE_BUFFER_SIZE = 1024; /* 写缓冲区的大小 */

    // HTTP请求报文的请求方法，本项目只用到GET和POST
    enum METHOD
    {
        GET = 0, /*请求读取url的信息*/
        POST,    /*给服务器添加信息，如注释*/
        HEAD,    /*请求读取url的信息首部*/
        PUT,     /*在指明的URL所标志的资源*/
        DELETE,  /*删除指明的URL所标志的资源*/
        TRACE,   /*环回测试的请求报文*/
        OPTIONS, /*请求一些选项信息*/
        CONNECT, /*用于代理服务器*/
        PATCH
    };

    // 主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, // 正在分析请求行
        CHECK_STATE_HEADER,          // 正在分析头部字段
        CHECK_STATE_CONTENT          // 分析请求体（实体主体）
    };

    /* 从状态机的状态：行的读取状态 */
    enum LINE_STATUS
    {
        LINE_OK = 0, // 读取到一个完整的行
        LINE_BAD,    // 行出错
        LINE_OPEN    // 行数据尚且不完整
    };

    /* 服务器处理HTTP请求的可能结果 */
    enum HTTP_CODE
    {
        NO_REQUEST = 0,    // 请求不完整，需要继续读取客户数据
        GET_REQUEST,       // 获得了一个完整的客户请求
        BAD_REQUEST,       // 客户请求有语法错误
        NO_RESOURCE,       // 没有资源
        FORBIDDEN_REQUEST, // 客户对资源没有足够的访问权限
        FILE_REQUEST,      // 文件请求
        INTERNAL_ERROR,    // 服务器内部错误
        CLOSED_CONNECTION  // 客户端已经关闭连接
    };

public:
    http_conn() {}
    ~http_conn() {}

    /* 初始化 新接受的连接 */
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true); /* 关闭连接 */
    void process();                          /* 处理客户请求 */
    bool read_once();                        /* 读取浏览器发来的全部数据，非阻塞读 */
    bool write();                            /* 响应报文写入，非阻塞写 */

    /* 获取此http连接的对方socket地址 */
    sockaddr_in *get_address()
    {
        return &m_address;
    }

    void initmysql_result(ConnectionPool *connPool);

    /**
     * 每个http连接有两个标志位：improv和timer_flag，初始时其值为0，它们只在Reactor模式下发挥作用。
     * Reactor模式下，当子线程执行读写任务出错时，来通知主线程关闭子线程的客户连接”。
     * improv: 保持主线程和子线程的同步；
     * time_flag: 标识子线程读写任务是否成功。
     */
    int timer_flag; /* 置1: reactor模式下，工作线程读写错误，然后WebServer::dealwithread中断开用户的连接, 从链表中删除对应timer*/
    int improv;     /* 置1: 标志着http连接的读写任务已完成（请求已处理完毕）*/

private:
    /* 初始化连接 */
    void init();

    /* 解析HTTP请求 */
    HTTP_CODE process_read();

    /* 填充HTTP应答 */
    bool process_write(HTTP_CODE ret);

    /* 下面这一组函数 被process_read调用以分析HTTP请求 */
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();

    /* 下面这一组函数 被process_write调用以填充HTTP应答 */
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line(); // 添加空白线

public:
    /*类静态数据成员，必须在类外部定义和初始化*/
    static int m_epollfd;    /* 所有socket上的事件都被注册到同一个epoll内核事件表中 */
    static int m_user_count; /* 统计用户数量 */
    MYSQL *mysql;
    int m_state;            /* 0：读， 1：写 */

private:
    int m_sockfd;          // 该http连接的socket
    sockaddr_in m_address; // 对方的socket地址

    char m_read_buf[READ_BUFFER_SIZE]; /* 读缓冲区(2048字节) */
    long m_read_idx;                    /* m_read_buf中已经读取的客户数据的最后一个字节的下一个位置 */
    long m_checked_idx;                 /* 当前已经分析完了m_read_buf中多少字节的客户数据 */
    int m_start_line;                  /* 当前正在解析的行在m_read_buf的起始位置 */

    char m_write_buf[WRITE_BUFFER_SIZE]; /* 写缓冲区(1024字节) */
    int m_write_idx;                     /* 写缓冲区中待发送的字节数 */

    CHECK_STATE m_check_state; /* 主状态机当前所处的状态 */
    METHOD m_method;           /* 请求方法 */

    char m_real_file[FILENAME_LEN]; /* 客户请求的目标文件的完整路径，其内容等于doc_root + m_url, doc_root是网站根目录 */
    char *m_url;                    /* 客户请求的目标文件的文件名 */
    char *m_version;                /* HTTP 协议版本号，我们仅支持HTTP/1.1 */
    char *m_host;                   /* 主机名 */
    long m_content_length;           /* HTTP请求的消息体长度 */
    bool m_linger;                  /* keep-alive ：HTTP请求是否要求保持连接 */

    char *m_file_address;    /* 客户请求的目标文件被mmap到内存的起始位置 */
    struct stat m_file_stat; /* 目标文件的状态，通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息 */
    struct iovec m_iv[2];    /* 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被读写内存块的数量 */
    int m_iv_count;

    int cgi;                    /* 是否启用POST */
    char *m_string;             /* 存储请求头数据 */
    int bytes_to_send;          // 剩余发送字节数
    int bytes_have_send;        // 已发送字节数
    char *doc_root;             /* 网站的根目录 */

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif // !HTTPCONNECTION_H