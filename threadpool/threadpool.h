#ifndef THREADPOOL_H
#define THREADPOOL_H


#include <list>
#include <cstdio>
// #include <stdio.h>
#include <pthread.h>
#include <exception>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

// 半同步/半反应堆 线程池
// 使用一个工作队列 完全解除 主线程、工作线程的耦合关系：主线程向工作队列中，插入任务，工作线程通过竞争来取得任务并执行它
// * 同步I/O模拟proactor模式
// * 半同步/半反应堆
// * 线程池

template <typename T>
class ThreadPool
{
public:
	/*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
	ThreadPool(int actor_model, ConnectionPool *connPool, int threadNumber = 8, int max_request = 10000);
	~ThreadPool();
	
	/* 往请求队列中添加任务 */
	bool append(T *request, int state);
	bool append_p(T *request);

private:
	/* 工作线程运行，它不断从工作队列中取出任务并执行 */
	static void *worker(void *arg);

	void run();

private:
	int m_thread_number;		// 线程池中的线程数
	int m_max_requests;			// 请求队列中 允许的最大请求数
	pthread_t *m_threads;		// 线程池数组，大小为m_thread_number

	std::list<T *> m_workqueue; // 请求队列（双向链表，任何位置插入和删除很快，但额外内存开销大）
	MutexLocker m_queuelocker;	// 互斥锁 (保护请求队列)
	Sem m_queuestat;			// 信号量，唤醒工作线程来竞争任务
	ConnectionPool *m_connPool; // 数据库连接池
    
	int m_actor_model;			// 模型切换(1:reactor  2:proactor)
};


template <typename T>
ThreadPool<T>::ThreadPool(int actor_model, ConnectionPool *connPool, int thread_number, int max_requests) : 
    m_actor_model(actor_model),             // 模型切换
    m_thread_number(thread_number),
    m_max_requests(max_requests),
    m_threads(NULL),                        // 线程池数组
    m_connPool(connPool)                    // 数据库连接池
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();

    /* 创建线程池数组，没有运行线程 */
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();

    /* 创建thread_number个线程，并将它们都设置为 脱离线程 */
    for (int i = 0; i < thread_number; ++i)
    {
        printf("create the %dth work thread\n", i);

        // C++中使用pthread_create(),第3个参数必须指向一个static静态函数
        // 在静态函数中，调用类成员函数, 通过传递类对象的this指针，从而在静态函数中调用对象成员函数
        // 创建8个新线程，运行线程函数 worker, 继而运行run()
        // 每个工作线程都阻塞在信号量上，等待此threadpool对象所在的主线程append工作任务
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }

        // 设置线程分离
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads; // 失败，则释放线程数组
            throw std::exception();
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
    delete[] m_threads;
}

/**主线程执行
 * 使用工作队列，完全解除主线程与工作线程的耦合关系：
 * 主线程往工作队列中插入任务，工作线程通过竞争来取得任务并执行它
 */
template <typename T>
bool ThreadPool<T>::append(T *request, int state)
{
    /* 操作工作队列时一定要加锁，因为它被所有线程共享 */
    m_queuelocker.lock();

    // 如果工作队列中，达到了最大请求数，则添加任务失败
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    // 工作队列还可以继续添加任务
    m_workqueue.push_back(request);

    /* 如果先唤醒，再解锁。则其他wait唤醒后，去拿锁失败，继续阻塞 */
    /* 所以要先解锁，在post唤醒，这样其他线程就能顺利拿到锁*/
    m_queuelocker.unlock();

    m_queuestat.post(); // 将信号量+1，唤醒其他正在等待信号量的工作线程(工作线程通过竞争)
    return true;
}

/**
 * proactor模式下，添加事件到请求队列
 */
template <typename T>
bool ThreadPool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    //
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

/* 工作线程 回调函数 */
template <typename T>
void *ThreadPool<T>::worker(void *arg)
{
    /* 传入的是ThreadPool对象的this指针，先强制转换为ThreadPool对象 */
    ThreadPool *pool = (ThreadPool *)arg;
    pool->run();
    return pool;
}

/* 工作线程执行 */
/* 新工作线程拿不到信号量 m_queuestat, 则阻塞 */
/* 主线程往工作队列中append任务后，调用post()唤醒这些阻塞在*/
template <typename T>
void ThreadPool<T>::run()
{
    while (true)
    {
        /* 所有工作线程阻塞在这里 */
        m_queuestat.wait();
        m_queuelocker.lock();
        
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }

        /* 取出工作队列中的队首任务元素 */
        T *request = m_workqueue.front(); // 获取首元素
        m_workqueue.pop_front();          // 删除首元素
        m_queuelocker.unlock();

        /* 判断取出的元素是不是空的 */
        if (!request)
        {
            continue;
        }

        // 1:reactor
        if (1 == m_actor_model)
        {
            /* 0 : 读状态 */
            if (0 == request->m_state)
            {
                /* read_once() ：循环读取客户数据，直到无数据可读 or 对方关闭连接 */
                if (request->read_once())
                {
                    request->improv = 1; /* 置1，标志着http连接的读写任务已完成（请求已处理完毕）*/

                    ConnectionRAII mysqlcon(&request->mysql, m_connPool); /* 从数据库连接池m_connPool中取出一个连接，传出给request->mysql */
                    request->process();                                   /* 执行HTTP请求的 process函数 */
                }
                else
                { /* 读数据出错*/
                    request->improv = 1;
                    request->timer_flag = 1; /* 0：定时器已删除，解绑客户端连接  1:定时器正绑定客户端连接 */
                }
            }
            else /* 1 : 写状态 */
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else /*写数据 出错*/
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        // 2:proactor
        else
        {
            ConnectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif