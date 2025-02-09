/**
 * 循环数组: 实现的阻塞队列，m_back = (m_back + 1) % m_max_size;
 * 线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
 */

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"

using namespace std;

/* 阻塞队列 模板类 */
template <class T>
class BlockQueue
{
public:
    BlockQueue(int maxSize = 100)
    {
        if (maxSize <= 0)
        {
            exit(-1);
        }
        mSizeMax = maxSize;
        mSize = 0; // 当前队列有0个元素
        mFront = -1;
        mBack = -1;
        mArray = new T[mSizeMax];   // 在动态内存分配 T类型数组
    }


    // 析构 ：释放 分配资源
    ~BlockQueue()
    {
        mMutex.lock(); // 多线程下，任何对资源的操作，一定要先加锁
        if (mArray != NULL)
        {
            delete[] mArray;
        }
        mMutex.unlock();
    }

    // 清除阻塞队列
    void clear()
    {
        mMutex.lock();
        mSize = 0;
        mFront = -1;
        mBack = -1;
        mMutex.unlock();
    }

    // 判断队列是否满了
    bool full()
    {
        // 读 mSize、mSizeMax 数据成员，也要加锁！
        mMutex.lock();
        if (mSize >= mSizeMax)
        {
            mMutex.unlock();
            return true;
        }
        mMutex.unlock();
        return false;
    }

    // 判断队列是否为空
    bool empty()
    {
        mMutex.lock();
        if (0 == mSize)
        {
            mMutex.unlock();
            return true;
        }
        mMutex.unlock();
        return false;
    }

    // 返回队首元素
    bool front(T &value)
    {
        mMutex.lock();
        if (0 == mSize)
        {
            mMutex.unlock();
            return false;
        }
        value = mArray[mFront];
        mMutex.unlock();
        return true;
    }

    // 返回队尾元素
    bool back(T &value)
    {
        mMutex.lock();
        if (0 == mSize)
        {
            mMutex.unlock();
            return false;
        }
        value = mArray[mBack];
        mMutex.unlock();
        return true;
    }

    // 往队列添加元素item，要先唤醒所有使用队列的线程
    // 当有元素push进队列, 相当于生产者生产了一个元素
    // 若当前没有线程等待条件变量, 则唤醒无意义
    bool push(const T &item)
    {
        mMutex.lock();
        // 队列满，无法添加元素时，先唤醒阻塞在队列资源上的所有线程
        if (mSize >= mSizeMax)
        {
            mCond.broadcast();
            mMutex.unlock();
            return false;
        }
        // 资源队列没有满
        mBack = (mBack + 1) % mSizeMax; // 循环数组, mBack指向下一个元素
        mArray[mBack] = item;           /* 这里是直接覆盖 原数组中的元素 */

        mSize++;

        mCond.broadcast(); /* 唤醒所有等待条件变量的线程, 多个线程去竞争1个条件变量, 没有竞争到的线程继续阻塞 */
        mMutex.unlock();
        return true;
    }

    // 抛弃队首元素，并获取队首的下一个元素
    // 当队列中没有元素，则等待条件变量唤醒
    bool pop(T &item)
    {
        mMutex.lock();

        /* 每push一个元素，mSize++，并唤醒所有线程 */
        /* 当队列中没有元素时，进入while循环代码 */
        while (mSize <= 0)
        {
            // 没有资源，等待条件变量，当前加锁的线程，先将互斥锁解锁，给其他线程拿到锁去生产push
            if (!mCond.wait(mMutex.get()))
            {
                /* 超时，进入此段代码，互斥锁解锁，返回pop失败 */
                mMutex.unlock();
                return false;
            }
        }

        /* 只有队列中元素个数 mSize > 0 才执行下面代码*/
        mFront = (mFront + 1) % mSizeMax; /* mFront指向下一个元素，相当于抛弃了前一个元素 */
        item = mArray[mFront];            /* 然后传出指向的下一个元素 */
        mSize--;
        mMutex.unlock();
        return true;
    }

    // 超时处理
    bool pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL); 

        mMutex.lock();
        
        /* 当队列中没有元素时, 则超时等待条件变量 */
        if (mSize <= 0)
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            /* 阻塞（解锁）等待条件变量，如果超时或有信号触发，线程唤醒 */
            /* 封装后 成功返回true、超时返回false */

            /**但是本线程 timewait() 解锁的时候，主线程和其他工作线程都有可能竞争到锁
             * 如果其他工作线程拿到锁，和本线程刚刚做的一样，依然进入上述代码，同样阻塞在timewait()上，从而继续释放锁
             * 这样可能有多个工作线程先后竞争到锁，然后阻塞在timewait()上
             * 所有阻塞在timewait()上工作线程，都是已经解锁的，没有拿到锁的状态
             * 之后，当主线程push拿到锁，就唤醒所有工作线程，然后解锁
             * 唤醒的所有工作线程去竞争锁，其中一个工作线程的 timewait() 成功返回，会将锁再次锁上，继续往下运行
             * 那么其他工作线程没有抢到锁，继续阻塞
             * */
            if (!mCond.timewait(mMutex.get(), t))
            {
                /* 超时，进入此段代码，互斥锁解锁，返回pop失败 */
                mMutex.unlock();
                return false;
            }
        }
        /* 运行到这里，有2种情况； 1. mSize > 0; 2. mSize <= 0,但等到条件变量信号，mSize++ = 1线程被唤醒运行到这里 */
        /* 情况2 ：这里我们还是拿着锁的，所以其他线程是阻塞在这个锁上的 */
        /* 为什么要再加一次判断？ 按理说其他线程没拿到锁，mSize至少是1？ */
        if (mSize <= 0)
        {
            mMutex.unlock();
            return false;
        }

        /* 只有队列中元素个数 mSize > 0 才执行下面代码*/
        mFront = (mFront + 1) % mSizeMax; /* mFront指向下一个元素，相当于抛弃了前一个元素 */
        item = mArray[mFront];            /* 然后传出指向的下一个元素 */
        mSize--;
        mMutex.unlock();
        return true;
    }

    //  返回当前阻塞队列中的元素个数
    int size()
    {
        int temp = 0;

        mMutex.lock();
        temp = mSize;

        mMutex.unlock();
        return temp;
    }

    // 返回阻塞队列的最大值
    int maxSize()
    {
        int maxSize = 0;

        mMutex.lock();
        maxSize = mSizeMax;

        mMutex.unlock();
        return maxSize;
    }

private:
    MutexLocker mMutex; // 多线程，确保同一时刻只有一个线程 操作 此阻塞队列对象 的数据成员
    Cond mCond;

    T *mArray;    // 队列数组(大小为mSizeMax，不可更改)
    int mSize;    // 队列的当前元素个数
    int mSizeMax; // 队列的最大值
    int mFront;   // 队列头部(索引，指向数组)
    int mBack;    // 队列尾部(索引，指向数组)
};

#endif // !BLOCK_QUEUE_H