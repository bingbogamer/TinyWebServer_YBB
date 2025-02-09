#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>   // 互斥锁、条件变量
#include <semaphore.h> // POSIX信号量
#include <exception> 

/* 封装 信号量*/
class Sem
{
public:
    /* 初始化信号量 */
    Sem(){
        // 0：信号量不在多个进程间共享   0：信号量初值
        if (sem_init(&mSem, 0, 0) != 0){
            throw std::exception();
        }
    }

    Sem(int num){
        if (sem_init(&mSem, 0, num) != 0){
            throw std::exception();
        }
    }

    /* 销毁信号量 */ 
    ~Sem(){sem_destroy(&mSem);}

    /* 等待信号量 */
    bool wait(){
        return sem_wait(&mSem) == 0;
    }

    /* 增加信号量 */
    bool post(){
        return sem_post(&mSem) == 0;
    }

private:
    sem_t mSem;
};


/* 封装 互斥锁 */
class MutexLocker
{
public:
    MutexLocker()
    {
        if (pthread_mutex_init(&mMutex, NULL) != 0)
        {
            throw std::exception();
        }
    }

    /* 销毁互斥锁 */
    ~MutexLocker()
    {
        pthread_mutex_destroy(&mMutex);
    }

    /* 加锁 */
    bool lock()
    {
        return pthread_mutex_lock(&mMutex) == 0;
    }

    /* 解锁 */
    bool unlock()
    {
        return pthread_mutex_unlock(&mMutex) == 0;
    }

    /* 获取当前的互斥锁对象 */
    pthread_mutex_t* get(){
        return &mMutex;
    }

private:
    pthread_mutex_t mMutex;
};


/* 封装条件变量类 */
class Cond
{
public:
    /* 创建并初始化一个条件变量 */
    Cond()
    {
        if (pthread_cond_init(&mCond, NULL) != 0){
            // pthread_mutex_destroy(&mMutex); // 先销毁分配的互斥锁
            throw std::exception();
        }
    }
    /* 销毁条件变量 */
    ~Cond()
    {
        // pthread_mutex_destroy(&mMutex); // 先销毁分配的互斥锁
        pthread_cond_destroy(&mCond);
    }

    /* 等待条件变量，如果没有信号触发，无限期等待下去 */
    /* 必须借助别的线程触发信号，否则线程自身无法唤醒 */
    /* 先把调用线程放入条件变量的等待队列中，然后将互斥锁解锁，函数成功返回时，互斥锁mutex将再次被锁 */
    bool wait(pthread_mutex_t *mutex)
    {
        int ret = 0;
        // pthread_mutex_lock(&mMutex);              //
        ret = pthread_cond_wait(&mCond, mutex);     // 先解锁，唤醒后再上锁
        // pthread_mutex_unlock(&mMutex);
        return ret == 0;
    }

    /* 等待条件变量，如果超时或有信号触发，线程唤醒 */
    bool timewait(pthread_mutex_t *mutex, struct timespec t){
        int ret = 0;
        // pthread_mutex_lock(&mMutex);                     //
        ret = pthread_cond_timedwait(&mCond, mutex, &t);    //
        // pthread_mutex_unlock(&mMutex);
        return ret == 0;
    }

    /* 唤醒等待 条件变量的线程 */
    bool signal()
    {
        return pthread_cond_signal(&mCond) == 0;
    }

    /* 唤醒所有等待此条件变量的线程 */
    bool broadcast(){
        return pthread_cond_broadcast(&mCond) == 0;
    }


private:
    pthread_cond_t mCond;
    // pthread_mutex_t mMutex;
};

#endif // !LOCKER_H
