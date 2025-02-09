#include "sql_connection_pool.h"

ConnectionPool::ConnectionPool()
{
    m_curConn = 0;
    m_freeConn = 0;
}

ConnectionPool *ConnectionPool::getInstance()
{
    static ConnectionPool connPool; // 局部静态变量(C++11，静态局部变量天然是线程安全)
    return &connPool;
}


/**
 * MaxConn ：设置数据库连接池的大小
 * url : localhost"
*/
void ConnectionPool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
    m_url = url;             // 主机地址
    m_port = Port;           // 数据库端口号
    m_user = User;           // 登录数据库用户名
    m_password = PassWord;   // 数据库密码
    m_databaseName = DBName; // 数据库名
    m_close_log = close_log; // 日志开关
    
    // 创建连接池： MaxConn个 数据库连接
    for (int i = 0; i < MaxConn; ++i)
    {
        MYSQL *connSql = NULL;
        connSql = mysql_init(connSql); // 初始化数据库
        if (connSql == NULL)
        {
            LOG_ERROR("mysql_init failed!");
			exit(1);    // 异常退出
        }
        
        // 用户名、密码登录数据库,连接到指定的db（默认端口）
        connSql = mysql_real_connect(connSql, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);
        if (connSql == NULL){
            LOG_ERROR("mysql_real_connect failed!");
            exit(1);
        }
        
        
        connList.push_back(connSql);    // 添加到Mysql连接池
        ++m_freeConn;
    }

    reserve = Sem(m_freeConn); // 设置信号量初值：m_FreeConn，即最大连接数MaxConn
    m_maxConn = m_freeConn;    // 最大连接数 = 当前空闲的连接数
}

// 从连接池 获取一个连接
// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
/* 消费者：信号量 */
MYSQL *ConnectionPool::getConnction()
{
    MYSQL *con = NULL;

    if (connList.size() <= 0)
        return NULL;

    reserve.wait(); /* 信号量-1，如果连接池用完，即信号量=0，则阻塞 */

    /* 运行在线程池的工作线程中，各个工作线程访问 单例数据库连接链表，加锁保护*/
    lock.lock();    // 对连接池操作，加锁

    con = connList.front();    /* 取出连接池中的1个连接 */
    connList.pop_front();

    --m_freeConn;  /* 空闲的连接数 -1 */
    ++m_curConn;   /* 已连接数 +1 */

    lock.unlock();
    return con;
}

/* 释放MYSQL连接, 重新添加到 连接池队列 */
bool ConnectionPool::releaseConnection(MYSQL *conn)
{
    if (NULL == conn)
        return false;

    /*多个工作线程访问代码，加锁保护*/
    lock.lock();

    // mysql_close(conn);

    connList.push_back(conn); // 重新添加到 连接池(list容器push_back)
    ++m_freeConn;
    --m_curConn;

    lock.unlock();

    reserve.post(); /* 信号量+1，唤醒阻塞线程 */
    return true;
}

/* 销毁所有连接 */
void ConnectionPool::destoryPool()
{
    lock.lock();
    if (connList.size() > 0)
    {
        list<MYSQL *>::iterator it;
        // 迭代器遍历数据库连接池链表，关闭所有连接的数据库
        for (it = connList.begin(); it != connList.end(); ++it)
        {
            mysql_close(*it);
        }

        // 再释放线程池资源
        connList.clear();
        m_freeConn = 0; /* 空闲连接数清零 */
        m_curConn = 0;  /* 已连接数清零 */
    }
    lock.unlock();
}

// ConnectionPool
int ConnectionPool::getFreeConn()
{
    return m_freeConn;
}

ConnectionPool::~ConnectionPool()
{
    destoryPool();
}

/*******************************************  RAII  **********************************************/

/**
 * 构造函数：从传入的数据库连接池中取1个连接
 * MYSQL **con : 指针的指针？如果是指针MYSQL *con，则指针实参 值传递，传入形参指针 和 实参指针 值相同，指向同一个对象
 * ConnectionPool *connPool : 传入的数据库连接池（单例模式）
 */
ConnectionRAII::ConnectionRAII(MYSQL **con, ConnectionPool *connPool)
{
    /* 改变传入的实参指针值，*/
    *con = connPool->getConnction(); // 从连接池 获取一个连接
    if (*con == NULL)
    {
        cout << "getConnction() return NULL" << endl;
    }
    conRAII = *con;
    poolRAII = connPool;    /* 单例 数据库连接池 */
}

/* 释放MYSQL连接, 重新添加到 连接池队列 */
ConnectionRAII::~ConnectionRAII()
{
    poolRAII->releaseConnection(conRAII);
}