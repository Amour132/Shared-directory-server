#include "ThreadPool.hpp"
#include "HttpServer.hpp"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sstream>
#include <unordered_map>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

std::unordered_map<std::string,std::string> g_mime_type = {
  {"unkonw",""},
  {"403","Forbidden"},
  {"400","Bad Request"},
  {"404","not found"},
  {"500","Server Error"},
  {"200","OK"},
};

const char* WWWROOT = "www";
const int MAXHEAD = 4096;
const int MAX_PATH = 256;

class RequestInfo
{
  //包含http-request解析出来的请求信息
  public:
    bool MethodIsLegal()
    {
      if(_method != "GET" && _method != "POST" && _method != "HEAD")
        return false;

      return true;
    }

    bool VersionIsLegal()
    {
      if(_version != "HTTP/0.9" && _version != "HTTP/1.0" && _version != "HTTP/1.1")
        return false;
      
      return true;
    }
    void SetErrNum(const std::string& code)
    {
      _err_code = code;
    }

    bool RequestIsCGI()
    {
      if((_method == "GET" && 1) || (_method == "POST"))
        return true;
      return false; 
    }
  public:
    std::string _method;//请求方法
    std::string _version;//版本号
    std::string _err_code;//错误信息
    struct stat _st; //获取文件信息的结构体 
    std::string _path_info; //相对路径
    std::string _path_phys; //绝对路径
    std::string _query_string; //查询字符串
    std::unordered_map<std::string,std::string> _hdr_list; //整个头部信息的键值对
};

class Utils 
{
  //对外提供一些工具接口
  public:
    static int Split(std::string& src,const std::string& seg,std::vector<std::string>& list )
    {
      int num = 0;
      int idx = 0;
      size_t pos = 0;
      while(idx < src.size())
      {
        pos = src.find(seg,idx);
        if(pos == std::string::npos)
          break;

        list.push_back(src.substr(idx,pos-idx));
        num++;
        idx = pos + seg.size();
      }
      if(idx < src.size())
      {
        list.push_back(src.substr(idx));
        num++;
      }
      return num;
    }

    static void TimeToGet(time_t t,std::string& code)
    {
      struct tm *mt = gmtime(&t);
      char tmp[128] = {0};
      size_t len = strftime(tmp,127,"%a, %d %b %Y %H:%M:%S GMT",mt);
      code.assign(tmp,len);
    }

    static void DigToStr(int64_t num,std::string& str)
    {
      std::stringstream ss;
      ss << num;
      str = ss.str();
    }
    
    static int64_t StrToDig(std::string& str) 
    {
      int64_t num;
      std::stringstream ss;
      ss << str;
      ss >> num;
      return num;
    }
    
    static void MakeEtag(int64_t size,int64_t ino,int64_t mtime,std::string& etag)
    {
      std::stringstream ss;
      ss << "\"" << std::hex << ino;
      ss << "_";
      ss << std::hex << size;
      ss << "_";
      ss << std::hex << mtime;
      ss << "\"";
      etag = ss.str();
    }

    static void GetTime(const std::string& file,std::string& mime)
    {
      size_t pos = file.find_last_of('.');
      if(pos == std::string::npos)
      {
        mime = g_mime_type["unknow"];
        return;
      }
      std::string suffix = file.substr(pos+1);
      auto it = g_mime_type.find(suffix);
      if(it == g_mime_type.end())
      {
        mime = g_mime_type["unkonw"];
        return;
      }
      else
      {
        mime = it->second;
      }
    }  
};

class HttpRequest
{
  //http数据接受的接口
  //http数据解析的接口
  //对外提供能获取处理结果的接口
  public:
    HttpRequest(int sock):_cli_sock(sock)
    {}

    //接受http请求
    bool RecvHttpHead(RequestInfo& info)
    {
      char tmp[MAXHEAD] = {0};
      while(1) 
      {
        //只读不拿出数据
        int ret = recv(_cli_sock,tmp,MAXHEAD,MSG_PEEK);
        if(ret <= 0)
        {
          if(errno == EINTR || errno == EAGAIN)
          {
            continue;
          }
          info.SetErrNum("500");
          return false;
        }
        char* str = strstr(tmp,"\r\n\r\n");
        if(str == NULL && (ret == MAXHEAD))
        {
          info.SetErrNum("413");
          return false;
        }
        else if(ret < MAXHEAD && str == NULL)
        {
          usleep(1000);
          continue;
        }

        int hdr_len = str-(char*)tmp;
        _http_head.assign(tmp,hdr_len);
        //缓冲区只剩\r\n\r\n+正文
        ret =  recv(_cli_sock,tmp,hdr_len+4,0);
        std::cout << tmp << std::endl;
        break;
      }
      return true;
    }

    bool PathIsLegal(std::string& path,RequestInfo& info) 
    {
      std::string file = WWWROOT + path;
      if(stat(file.c_str(),&info._st) < 0)
      {
        info.SetErrNum("404");
        return false;
      }
      char tmp[MAX_PATH] = {0};
      realpath(file.c_str(),tmp);
      info._path_phys = tmp;
      if(info._path_phys.find(WWWROOT) == std::string::npos)
      {
        info.SetErrNum("403");
        return false;
      }
      return true;
    }

    bool ParseFristLine(std::string& line,RequestInfo& info)
    {
      std::vector<std::string> line_list;
      if(Utils::Split(line," ",line_list) != 3)
      {
        info.SetErrNum("400");
        return false;
      }
      std::string url;
      info._method = line_list[0];
      url = line_list[1];
      info._version = line_list[2];

      if(info.MethodIsLegal() == false)
      {
        info.SetErrNum("405");
        return false;
      }
      if(info.VersionIsLegal() == false) 
      {
        info.SetErrNum("400");
        return false;
      }
      size_t pos = 0;
      pos = url.find('?');
      if(pos == std::string::npos)
      {
        info. _path_info = url;
      }
      else 
      {
        info._path_info = url.substr(0,pos);
        info._query_string = url.substr(pos+1);
      }
      return PathIsLegal(info._path_info,info);
    }

    //解析http请求
    bool ParseHttpHead(std::string& head)
    {
      //请求方法 URL 版本
      std::vector<std::string> hdr_list;
      Utils::Split(_http_head,"\r\n",hdr_list);
      if(ParseFristLine(hdr_list[0],_info) == false)
      {
        return false;
      }
    
      for(size_t i = 1; i < hdr_list.size(); i++)
      {
        size_t pos = hdr_list[i].find(':');
        _info._hdr_list[hdr_list[i].substr(0,pos)] = hdr_list[i].substr(pos+2);
      }
      for(auto it = _info._hdr_list.begin(); it != _info._hdr_list.end(); it++)
      {
        std::cout << "[" << it->first << "]=[" << it->second << "]" << std::endl;
      }
      return true;
    }
  private:
    std::string  _http_head; //http头部
    int _cli_sock;
    RequestInfo _info;
    std::vector<std::string> _hdr_list;
};

class HttpResponse 
{
  public:
    HttpResponse(int sock):_cli_sock(sock) 
    {}

    bool HttpResponseInit(RequestInfo& info)
    {
      info._st.st_size;
      info._st.st_ino;
      info._st.st_mtim;
      Utils::DigToStr(info._st.st_mtim,_mtime);
      Utils::MakeEtag(info._st.st_size,info._st.st_ino,info._st.st_mtim);

      time_t t = time(NULL);
      Utils::TimeToGet(t,_date);

      return true;
    }
    bool ErrHandler(RequestInfo& info) 
    {

      return true;
    }
    bool FileHandler(RequestInfo& info) 
    {
      return true;
    }

  private:
    int _cli_sock;
    std::string _etge;//文件是否被修改
    std::string _mtime;//最后一次修改时间
    std::string _date; //系统最后一次响应时间
    std::string _mime;;
};


