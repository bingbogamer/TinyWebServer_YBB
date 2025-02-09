/*******************************************************
 * 单例模式创建日志系统，对服务器运行状态、错误信息和访问数据进行记录，
 * 该系统可以实现按天分类，超行分类功能，可以根据实际情况分别使用同步和异步写入两种方式。
 ********************************************************/

#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

// 单例模式
class Log
{
public:
    // 静态成员函数，与类对象无关，不能通过类对象调用
    // C++11，局部变量 懒汉不用加锁
    static Log *getInstance()
    {
        static Log instance; // 局部静态成员函数：
        return &instance;
    }



    // 静态成员函数，与类对象无关，不能通过类对象调用
    /* 调用异步日志，向Log文件中写入一条日志 */
    static void *flush_log_thread(void *)
    {
        Log::getInstance()->async_write_log();
    }

    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int spilt_lines = 5000000,
              int max_queue_size = 0);


    /** 同步日志 （增加API的响应时间。日志记录操作需要占用主线程的资源）
     * 日志写入函数与工作线程串行执行，
     * 由于涉及到I/O操作，当单条日志比较大的时候，同步模式会阻塞整个处理流程，
     * 服务器所能处理的并发能力将有所下降，尤其是在峰值的时候，写日志可能成为系统的瓶颈 */
    void write_log(int level, const char *format, ...);

    void flush();

private:
    Log();          // 私有构造函数
    virtual ~Log(); // 虚析构函数

    /* 异步日志 
    （将日志记录操作放到另一个线程或进程中进行，而不会阻塞主线程）
    （日志记录操作不会影响主线程的执行，使得主线程能够更快地响应客户端请求）
    将所写的日志内容先存入阻塞队列，写线程从阻塞队列中取出内容，写入日志 */
    void *async_write_log()
    {
        string single_log;
        // 从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);    // 向Log文件中写入日志，
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; // Log文件保存路径
    char log_name[128]; // log文件名
    int m_split_lines;  // 日志文件最大行数（每一个日志文件）
    int m_log_buf_size; // 日志缓冲区大小(先存放在该缓冲区中)
    long long m_count;  // 日志行数记录（仅当天）
    int m_today;        // 因为按天分类,记录当前时间是那一天
    FILE *m_fp;         // 打开log的文件指针
    char *m_buf;        // 日志缓冲区数组指针

    // （异步日志，将所写的日志内容先存入阻塞队列，写线程从阻塞队列中取出内容，写入日志）
    BlockQueue<string> *m_log_queue; // 阻塞队列（用于异步日志）
    bool m_is_async;                 // 是否异步标志位
    MutexLocker m_mutex;             // 互斥锁（写日志文件的同步）
    int m_close_log;                 // 关闭日志
};

#define LOG_DEBUG(format, ...)                                   \
    if (0 == m_close_log)                                        \
    {                                                            \
        Log::getInstance()->write_log(0, format, ##__VA_ARGS__); \
        Log::getInstance()->flush();                             \
    }

#define LOG_INFO(format, ...)                                    \
    if (0 == m_close_log)                                        \
    {                                                            \
        Log::getInstance()->write_log(1, format, ##__VA_ARGS__); \
        Log::getInstance()->flush();                             \
    }

#define LOG_WARN(format, ...)                                    \
    if (0 == m_close_log)                                        \
    {                                                            \
        Log::getInstance()->write_log(2, format, ##__VA_ARGS__); \
        Log::getInstance()->flush();                             \
    }

#define LOG_ERROR(format, ...)                                   \
    if (0 == m_close_log)                                        \
    {                                                            \
        Log::getInstance()->write_log(3, format, ##__VA_ARGS__); \
        Log::getInstance()->flush();                             \
    }

#endif // !LOG_H
