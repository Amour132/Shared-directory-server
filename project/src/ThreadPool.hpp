#pragma once 

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <unordered_map>
#include <queue>
#include <unistd.h>


typedef bool(*Handler)(int  sock);

class HttpTask
{
  //Http请求处理的任务
  //处理接口 
  public:
    HttpTask():_cli_sock(-1){}
    HttpTask(int sock,Handler handler):_cli_sock(sock),_task(handler) 
    {}

    bool SetTask(int sock,Handler handler)
    {
      _cli_sock = sock;
      _task = handler;
      return true;
    }

    void Run()
    {
      _task(_cli_sock);
    }
  private:
    int _cli_sock;
    Handler _task;
};

class ThreadPool 
{
  public:
    ThreadPool(int num):_thread_num(num),_is_stop(false)
    {}

    ~ThreadPool()
    {
      pthread_mutex_destroy(&_mutex);
      pthread_cond_destroy(&_cond);
    }

    bool InitThreadPool()
    {
      pthread_t tid;
      for(int i = 0; i < _thread_num; i++)
      {
        int ret = pthread_create(&tid,NULL,thr_start,this);
        if(ret < 0)
        {
          std::cerr << "pthread create error" << std::endl;
          return false;
        }
        pthread_detach(tid);
      }
      pthread_mutex_init(&_mutex,NULL); 
      pthread_cond_init(&_cond,NULL);  
      return true;
    }

    void IdleThread()
    {
      if(_is_stop)
      {
        QueueUnlock();
        _thread_num--;
        pthread_exit((void*)0);
        return;
      }
      pthread_cond_wait(&_cond,&_mutex);
    }

    bool PushTask(HttpTask& t)
    {
      QueueLock();
      if(_is_stop)
        QueueUnlock();

      _queue.push(t);
      SingalOne();
      QueueUnlock();
      return true;
    }

    bool PopTask(HttpTask& t)
    {
      t = _queue.front();
      _queue.pop();
      return true;
    } 

    bool IsEmpty()
    {
      return _queue.size() == 0;
    }

    void QueueLock()
    {
      pthread_mutex_lock(&_mutex);
    }

    void QueueUnlock()
    {
      pthread_mutex_unlock(&_mutex);
    }

    void Destory()
    {
      if(!_is_stop) 
        _is_stop = true;

      while(_thread_num > 0)
        SignalAll();
    }
  private:

    static void* thr_start(void* arg) 
    {
      ThreadPool* tp = (ThreadPool*)arg;
      while(1)
      {
        tp->QueueLock();
        while(tp->IsEmpty())
        {
          tp->IdleThread();
        }
        HttpTask t;
        tp->PopTask(t);
        tp->QueueUnlock();

        t.Run();
      }
    }

    void ThreadWait()
    {
      pthread_cond_wait(&_cond,&_mutex);
    }

    void SingalOne()
    {
      pthread_cond_signal(&_cond);
    }
    
    void SignalAll()
    {
      pthread_cond_broadcast(&_cond);
    }

  private:
    int _thread_num; //线程数量
    std::queue<HttpTask> _queue; //任务队列
    bool _is_stop; //线程池是否要停止 
    pthread_mutex_t _mutex;//互斥锁 
    pthread_cond_t _cond; //条件变量 
};

