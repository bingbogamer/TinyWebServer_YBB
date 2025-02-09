#include <time.h>
#include <sys/time.h>
#include <string.h>

#include "log.h"

// 单例模式类
// 构造函数：
Log::Log()
{
    m_count = 0;            // 日志行数记录
    m_is_async = false;     // 是否异步标志位（默认同步）
}


Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);    // 关闭文件
    }
}

/* 异步日志： 将所写的日志内容先存入阻塞队列，写线程从阻塞队列中取出内容，写入日志 */
// 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    // 如果设置了 max_queue_size, 则为异步日志（在另一个线程中，执行日志写入）
    if (max_queue_size >= 1)
    {
        m_is_async =true;
        // 创建阻塞队列对象，大小为max_queue_size，m_log_queue指向该对象
        m_log_queue = new BlockQueue<string>(max_queue_size);
        pthread_t tid;

        // 创建线程 回调函数 flush_log_thread，异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;      
    m_buf = new char[m_log_buf_size];   // 日志缓冲区，char数组
    memset(m_buf, '\0', m_log_buf_size);    // 将buf中的每个字节设置为空字符
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);  // 当前的本地时间和日期
    struct tm my_tm = *sys_tm;

    // 查找file_name所指向的字符串中，最后一次出现字符 '/'的位置,返回指向最后一次出现的位置
    const char *p = strrchr(file_name, '/');
    // Log文件的全路径名
    char log_full_name[256] = {0};

    // 不存在字符'/'
    if (p == NULL)
    {
        // 将以 年-月-日-文件名 格式，以'\0'结尾的字符串，存储到 log_full_name缓冲区中
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        // 取出file_name字符串中的 文件名作为log_name ： 将'/'后面的字符串 复制(覆盖)到log_name
        strcpy(log_name, p + 1);   

        // 取出目录名 ：将file_name开头到‘/’为止的字符，拷贝到dir_name中
        strncpy(dir_name, file_name, p - file_name + 1);    

        // 目录名-年-月-日-log_name 格式，存储到 log_full_name缓冲区中
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    // 追加方式，打开文件，如果文件不存在，则创建文件
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}


/* 同步写日志*/
void Log::write_log(int level, const char *format, ...)
{
    // 重新获取一下当前写日志的时间
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);   
    time_t t = now.tv_sec;

    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    char s_level[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s_level, "[DEBUG]:");
        break;
    case 1:
        strcpy(s_level, "[INFO]:");
        break;
    case 2:
        strcpy(s_level, "[WARN]:");
        break;
    case 3:
        strcpy(s_level, "[ERROR]:");
        break;
    default:
        strcpy(s_level, "[INFO]:");
        break;
    }

    // 写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++;      // 所有记录日志的行数

    // everyday log
    // m_today != my_tm.tm_mday : 
    // m_count % m_split_lines == 0 : 
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
    {
        char new_log_full_name[256] = {0};    // 新的日志名
        fflush(m_fp);   // 刷新缓冲区
        fclose(m_fp);   // 关闭旧的日志文件

        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        // 如果当前的天数变化，新建日志名
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log_full_name, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;    // 重置新一天的日志记录行数
        }
        // 如果日志最大行数超过m_split_lines
        else    
        {
            snprintf(new_log_full_name, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }

        m_fp = fopen(new_log_full_name, "a");
    }
    m_mutex.unlock();

    // 
    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    // 写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s_level);

    // 向一个字符串缓冲区打印格式化字符串，且可以限定打印的格式化字符串的最大长度
    // 向m_buf + n后的位置，写入m_log_buf_size - n - 1个字符，最后一个字符保留为NULL空字符
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    // 阻塞队列没有满 && 异步日志
    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str); // 将日志放入阻塞队列
    }
    // 同步模式 或者 异步日志的阻塞队列已经满，则直接将日志写入文件
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    // 强制刷新， 写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}


