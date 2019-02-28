#pragma once 

#include "Utils.hpp"
#include "ThreadPool.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>

const int MAX_THREAD = 10;

class HttpServer
{
  //创建一个TCP服务端程序，接受新连接
  //为新的链接组织一个线程池的任务，添加到线程池中
  public:
    HttpServer():_ser_sock(-1)
    {}

    void Close()
    {
      close(_ser_sock);
    }

    ~HttpServer()
    {
      Close();
    }

    HttpServer(std::string ip,int port):_ip(ip),_port(port) 
    {}

    bool HttpInit()//Tcp服务端初始化和线程池的初始化
    {
      _ser_sock = socket(AF_INET,SOCK_STREAM,0);
      if(_ser_sock < 0)
      {
        std::cerr <<"socket error"<< std::endl;
        return false;
      }

      //可重复绑定，服务端可以立即重启
      int opt = 1;
      setsockopt(_ser_sock,SOL_SOCKET,SO_REUSEADDR,(const void*)&opt,sizeof(opt));

      sockaddr_in lst_addr;
      lst_addr.sin_family = AF_INET;
      lst_addr.sin_port = htons(_port);
      lst_addr.sin_addr.s_addr = inet_addr(_ip.c_str());
      socklen_t len = sizeof(lst_addr); 

      if(bind(_ser_sock,(sockaddr*)&lst_addr,len) < 0)
      {
        std::cerr << "bind error" << std::endl;
        return false;
      }
      if(listen(_ser_sock,MAX_THREAD) < 0)
      {
        std::cerr << "listen error" << std::endl;
        return false;
      }

      _tp = new ThreadPool(MAX_THREAD);
      if(_tp == NULL) 
      {
        std::cerr << "new ThreadPool Error" << std::endl;
        return false;
      }
      if(_tp->InitThreadPool() == false) 
      {
        std::cerr << "Init Pool error" << std::endl;
        return false;
      }

      return true;
    }

    bool Start()
    {
      while(1)
      {
        sockaddr_in cli_addr;
        socklen_t len = sizeof(sockaddr_in);
        int sock = accept(_ser_sock,(sockaddr*)&cli_addr,&len);
        if(sock < 0)
        {
          std::cerr << "accept error" << std::endl;
          return false;
        }
        HttpTask t;
        t.SetTask(sock,HttpHandler);
        _tp->PushTask(t);
      }
      return true;
    }
  private:
    static bool (HttpHandler)(int sock)// Http任务处理 
    {
      RequestInfo info;
      std::string head;
      HttpRequest req(sock);
      HttpResponse rsp(sock);

      //接受头部
      if(req.RecvHttpHead(info) == false)
      {
        rsp.ErrHandler(info);
        return false;
      }
      //解析http头部 
      if(req.ParseHttpHead(info) == false)
      {
        rsp.ErrHandler(info);
        return false;
      }
      if(info.RequestIsCGI())
      {
        //如果请求类型是CGI请求，则执行CGI响应
        rsp.CGIHandler(info);
      }
      else//不是，执行目录列表/文件下载 
        rsp.FileHandler(info); 
      return true;
    }
    
  private:
    int _ser_sock;
    ThreadPool* _tp;
    std::string _ip;
    int _port;
};


