#ifndef CONNECTION_POOL
#define CONNECTION_POOL

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

/**
 * 数据库连接池（单例模式）
*/
class ConnectionPool
{
public:
    MYSQL *getConnction();               // 获取数据库连接
    bool releaseConnection(MYSQL *conn); // 释放连接
    int getFreeConn();                   // 获取连接
    void destoryPool();                  // 销毁所有连接

    /*初始化*/
    void init(string url, string user, string password, string databaseName, int port, int maxConn, int closeLog);

    // 单例模式
    static ConnectionPool *getInstance();

private:
    ConnectionPool();  /* 单例模式：私有构造函数，无法创建对象 */
    ~ConnectionPool();

    int m_maxConn;          // 最大连接数
    int m_curConn;          // 当前已使用的连接数
    int m_freeConn;         // 当前空闲的连接数
    MutexLocker lock;       // 互斥锁（访问公共资源）
    list<MYSQL *> connList; // 连接池链表
    Sem reserve;            // 信号量

public:
    string m_url;          // 主机地址（字符名）
    string m_port;         // 数据库端口号
    string m_user;         // 登录数据库用户名
    string m_password;     // 数据库密码
    string m_databaseName; // 数据库名
    int m_close_log;       // 日志开关
};



/**
 * RAII(Resource Acquisition Is Initialization “资源获取初始化”)
*/
class ConnectionRAII
{
public:
    ConnectionRAII(MYSQL **con, ConnectionPool *connPool);
    ~ConnectionRAII();

private:
    MYSQL *conRAII;
    ConnectionPool *poolRAII;   /*数据库连接池 对象指针*/
};



#endif // !CONNECTION_POOL