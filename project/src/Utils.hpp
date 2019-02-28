#pragma once

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
#include <dirent.h>
#include <fcntl.h>

std::unordered_map<std::string,std::string> g_mime_type = {
  {"unikonw","application/coete-stream"},
  {"txt","application/coete-stream"},
  {"html","text/html"},
  {"htm","text/html"},
  {"jpg","image/jpeg"},
  {"zip","application/zip"},
  {"mp3","audio/mpeg"},
};
std::unordered_map<std::string,std::string> g_error_des = {
  {"unkonw",""},
  {"403","Forbidden"},
  {"400","Bad Request"},
  {"404","Not Found"},
  {"405","Method Not Allowed"},
  {"413","Requesr Entity Too Large"},
  {"500","Server Error"},
  {"200","OK"},
};

const char* WWWROOT = "www";
const int MAXHEAD = 4096;
const int MAX_PATH = 256;
const int MAX_BUFF = 4096;

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
      if((_method == "GET" && !_query_string.empty()) || (_method == "POST"))
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

    static void GetMine(const std::string& file,std::string& mime)
    {
      size_t pos = file.find_last_of(".");
      if(pos == std::string::npos)
      {
        mime = g_mime_type["unknow"];
        return;
      }
      std::string suffix = file.substr(pos+1);
      auto it = g_mime_type.find(suffix);
      if(it == g_mime_type.end())
      {
        mime = g_mime_type["unknow"];
      }
      else 
        mime = it->second;
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
    
    static const std::string GetErrDes(std::string& code)
    {
      auto it = g_error_des.find(code);
      if(it == g_error_des.end())
      {
        return "Unknow error";
      }
      return it->second;
    }
    static void MakeEtag(int64_t size,int64_t ino,time_t  mtime,std::string& etag)
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
        std::cout << "248Path Is Legal 404" << std::endl;
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
    bool ParseHttpHead(RequestInfo& info)
    {
      //请求方法 URL 版本
      std::vector<std::string> hdr_list;
      Utils::Split(_http_head,"\r\n",hdr_list);
      if(ParseFristLine(hdr_list[0],info) == false)
      {
        return false;
      }
    
      for(size_t i = 1; i < hdr_list.size(); i++)
      {
        size_t pos = hdr_list[i].find(':');
        info._hdr_list[hdr_list[i].substr(0,pos)] = hdr_list[i].substr(pos+2);
      }
      for(auto it = info._hdr_list.begin(); it != info._hdr_list.end(); it++)
      {
        //std::cout << "[" << it->first << "]=[" << it->second << "]" << std::endl;
      }
      return true;
    }
  private:
    std::string  _http_head; //http头部
    int _cli_sock;
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
      info._st.st_mtime;
      Utils::TimeToGet(info._st.st_mtime,_mtime);
      Utils::MakeEtag(info._st.st_size,info._st.st_ino,info._st.st_mtime,_etag);

      time_t t = time(NULL);
      Utils::TimeToGet(t,_date);
      Utils::DigToStr(info._st.st_size,_fsize);
      Utils::GetTime(info._path_phys,_mime);

      return true;
    }

    bool FileIsDir(RequestInfo& info)
    {
      if(info._st.st_mode & S_IFDIR)
      {
        if(info._path_info[info._path_info.size()-1] != '/')
        {
          info._path_info.push_back('/');
        }
        if(info._path_phys[info._path_phys.size()-1] != '/')
        {
          info._path_phys.push_back('/');
        }
        return true;
      }
      return false;
    }

    bool ProcessList(RequestInfo& info) //文件列表展示
    {
      //组织头部
      //首行
      //Content-Type:text/html\r\n
      //Etag: \r\n
      //Date: \r\n
      //正文
      std::string rsp_head;
      std::string rsp_body;
      rsp_head = info._version + " 200 OK\r\n";
      rsp_head += "Content-Type: text/html\r\n";
      rsp_head += "Connection: close\r\n";
      if(info._version == "HTTP/1.1")
      {
        rsp_head += "Transfer-Encoding: chunked\r\n";
      }
      rsp_head += "Etag: " + _etag + "\r\n";
      rsp_head += "Last-Modified: " + _mtime + "\r\n";
      rsp_head += "Date: " + _date + "\r\n\r\n";
      SendData(rsp_head);

      //std::cout << rsp_head << std::endl;

      rsp_body = "<html><head>";
      rsp_body += "<title>Index of" + info._path_info + "</title>";
      rsp_body += "<meta charset='UTF-8'>";
      rsp_body += "</head><body>";
      rsp_body += "<h1>Index of" + info._path_info + "</h1>";
      rsp_body += "<form action='/upload' method='POST' ";
      rsp_body += "enctype='multipart/form-data'>";
      rsp_body += "<input type='file' name='FileUpload' />";
      rsp_body += "<input type='submit' value='上传' />";
      rsp_body += "</form>";
      rsp_body += "<hr /><ol>";
      SendCData(rsp_body);

      //std::cout << rsp_body <<std::endl;
      

		  struct dirent** p_dirent = NULL;
			  //获取目录下的每一个文件组织html信息，chunke传输
			int num = scandir(info._path_phys.c_str(), &p_dirent, 0, alphasort);
    
			for(int i = 0; i < num; i++)
			{
        std::string file_html;
				std::string  file;
				file = info._path_phys + p_dirent[i]->d_name;
				struct stat st;
				if (stat(file.c_str(), &st) < 0)
					continue;

				std::string mtime;
				Utils::TimeToGet(st.st_mtime, mtime);
				std::string mime;
				Utils::GetMine(p_dirent[i]->d_name, mime);

				std::string fsize;
				Utils::DigToStr(st.st_size/1024, fsize);

				file_html += "<li><strong><a href='" + info._path_info;
				file_html += p_dirent[i]->d_name;
				file_html += "'>";
				file_html += p_dirent[i]->d_name;
        file_html += "</a><strong>";
				file_html += "<br/ ><small>";
				file_html += "modified: " + mtime + "<br />";
				file_html += mime + " - " + fsize + "kbytes";
				file_html += "<br /><br /></small></li>";

        SendCData(file_html);
        //std::cout << file_html << std::endl;
      }
			rsp_body = "</ol><hr /></body></html>";
			SendCData(rsp_body);
      SendCData("");
      //std::cout << rsp_body << std::endl;
      return true;
    }

    bool ErrHandler(RequestInfo& info) 
    {
      //首行 协议版本+状态码+状态描述 \r\n
      //头部 ContentLength Date
      //空行
      //正文
      std::string rep_head;
      rep_head += info._version + " " + info._err_code + " ";
      rep_head += Utils::GetErrDes(info._err_code) + "\r\n";

      time_t t = time(NULL);
      std::string gmt;
      Utils::TimeToGet(t,gmt);
      rep_head += "Date: " + gmt + "\r\n";

      std::string rep_body;
      rep_body = "<html><body><h1>" + info._err_code + "</h1></body></html>";
      std::string cont_len;
      Utils::DigToStr(rep_body.size(),cont_len);

      rep_head += "Content-Length: " + cont_len + "\r\n\r\n";

      //std::cout << rep_head << std::endl;
      //std::cout << rep_body << std::endl;
      SendData(rep_head);
      SendData(rep_body);
      return true;
    }

    bool ProcessFile(RequestInfo& info)
    {
      std::string rsp_head;
      rsp_head = info._version + " 200 OK\r\n";
      rsp_head += "Content-Type:" + _mime + "\r\n";
      rsp_head += "Content-Length: " + _fsize + "\r\n";
      rsp_head += "ETag: " + _etag + "\r\n";
      rsp_head += "Last-Modified: " + _mtime + "\r\n";
      rsp_head += "Date: " + _date + "\r\n\r\n";
      SendData(rsp_head);

      int fd = open(info._path_phys.c_str(),O_RDONLY);
      if(fd < 0)
      {
        info._err_code = "400";
        ErrHandler(info);
        return false;
      }
      int rlen = 0;
      char tmp[MAX_BUFF];
      while((rlen = read(fd,tmp,MAX_BUFF)) > 0)
      {
        tmp[rlen] = '\0';
        SendData(tmp);
        send(_cli_sock,tmp,rlen,0);
      }
      close(fd);
      return true;
    }
    bool ProcessCGI(RequestInfo& info)
    {
      //使用外部程序完成CGI请求-文件上传
      //将http头部信息和正文交给子进程处理
      //使用环境变量传递头信息
      //使用管道传递正文数据
      //使用管道接受CGI程序的处理结果

 
      int in_[2]; 
      int out[2];
      if(pipe(in_) || pipe(out))
      {
        info.SetErrNum("500");
        ErrHandler(info);
        return false;
      }
      int pid = fork();
      if(pid < 0)
      {
        info.SetErrNum("500");
        ErrHandler(info);
        return false;
      }
      else if (pid == 0)//子进程 
      {

        setenv("METHOD",info._method.c_str(),1);
        setenv("VERSION",info._version.c_str(),1);
        setenv("PATH_INFO",info._path_info.c_str(),1);
        setenv("QUERY_STRING",info._query_string.c_str(),1);
        
        for(auto it = info._hdr_list.begin(); it != info._hdr_list.end(); it++)
        {
          setenv(it->first.c_str(),it->second.c_str(),1); 
        }
        close(in_[1]);
        close(out[0]); 

        

        //重定向
        //子进程打印处理结果传递给父进程
        //子进程从标准输入读数据
        dup2(out[1],1);
        dup2(in_[0],0); 

        execl(info._path_phys.c_str(),info._path_phys.c_str(),NULL);
      
        exit(0);
      }
      //父进程 
      //将正文数据传递给子进程，通过out管道从子进程读取结果
      //将处理结果组织成http数据响应给客户端
      close(in_[0]);
      close(out[1]);

      auto it = info._hdr_list.find("Content-Length");
      if(it == info._hdr_list.end())
      {
        //不需要提交正文数据
        char buff[MAX_BUFF];
        int64_t content_len = Utils::StrToDig(it->second);
        int tlen = 0;
        while(tlen < content_len)
        {
          int len = (content_len-tlen) > MAX_BUFF ? MAX_BUFF :(content_len - tlen);
          
          int rlen = recv(_cli_sock,buff,len,0); 
          if(rlen < 0)
          {
            info.SetErrNum("405");
            ErrHandler(info);
            return false;

          }
          tlen += rlen;
          if(write(in_[1],buff,rlen) < 0)
            return false;
        }
      }

      //组织头部
      std::string rsp_head;
      rsp_head = info._version + " 200 OK\r\n";
      rsp_head += "Content-Type: text/html\r\n";
      rsp_head += "Connection: close\r\n";
      rsp_head += "ETag: " + _etag + "\r\n";
      rsp_head += "Last-Modified: " + _mtime + "\r\n";
      rsp_head += "Date: " + _date + "\r\n\r\n";
      SendData(rsp_head);
      

      while(1)
      {
        char buff[MAX_BUFF] = {0};
        int rlen = read(out[0],buff,MAX_BUFF);
        std::cerr << rlen << std::endl;
        if(rlen == 0)
          break;
        send(_cli_sock,buff,rlen,0);
     }

      close(in_[1]);
      close(out[0]);
      return true;

    }

    bool FileHandler(RequestInfo& info) 
    {
      HttpResponseInit(info);
      if(FileIsDir(info))
      {
        ProcessList(info);
      }
      else 
        ProcessFile(info);
      return true;
    }

    bool CGIHandler(RequestInfo& info)
    {
      HttpResponseInit(info);
      ProcessCGI(info);
    }
    
  private:
    bool SendData(const std::string& buff)
    {
      if(send(_cli_sock,buff.c_str(),buff.size(),0) < 0)
        return false;
      return true;
    }

    bool SendCData(const std::string& buff)
    {
      if(buff.empty())
      {
        return SendData("0\r\n\r\n");
      }
      std::stringstream ss;
      ss << std::hex << buff.size();
      ss << "\r\n";

      SendData(ss.str());
      SendData(buff);
      SendData("\r\n");
      return true;
    }

  private:
    int _cli_sock;
    std::string _etag;//文件是否被修改
    std::string _mtime;//最后一次修改时间
    std::string _date; //系统最后一次响应时间
    std::string _mime; 
    std::string _fsize;
};

enum _boundry_type
{
  BOUNDRY_NO=0,
  BOUNDRY_FIRST,
  BOUNDRY_MIDDLE,
  BOUNDRY_LAST,
  BOUNDRY_BAK
};

class Upload
{
  public:
    Upload():_file_fd(-1)
    {}

    bool InitUpload()
    {
      umask(0);
      char* ptr = getenv("Content-Length");

      std::string ret = ptr;

      if(ptr == nullptr)
      {
        fprintf(stderr,"have no Content-Length\n");
        return false;
      }
      ret = ptr;
       _content_len = Utils::StrToDig(ret);


      ptr = getenv("Content-Type");
    
      if(ptr == nullptr)
      {
        fprintf(stderr,"have no Content-Type\n");
        return false;
      }

      std::string boundry_sep = "boundary=";
      std::string content_type = ptr;

      size_t pos = content_type.find(boundry_sep);
      if(pos == std::string::npos)
      {
        fprintf(stderr,"Content-Type have no boundary\n");
        return false;
      }
      std::string boundry_str = content_type.substr(pos+boundry_sep.size());
 
      _f_boundry = "--" + boundry_str;
      _m_boundry = "\r\n" + _f_boundry + "\r\n";
      _l_boundry = "\r\n" + _f_boundry + "--";

      std::cerr << _f_boundry << std::endl;
      std::cerr << _m_boundry << std::endl;
      std::cerr << _l_boundry << std::endl;

      return true;
    }

    bool ProcessUpLoad()
    {
      std::cerr << _content_len << std::endl;
      int64_t tlen = 0;
      int64_t blen = 0;
      char buff[MAX_BUFF];
      while(tlen < _content_len)
      {
        std::cerr << blen << std::endl;
        int len = read(0,buff+blen,MAX_BUFF-blen);
        std::cerr << len << std::endl;
        blen += len;//当前buff的数据长度 
        int boundry_pos;
        int content_pos;

      

        if(MatchBoundry(buff,blen,&boundry_pos) == BOUNDRY_FIRST)
        {
          //从boudry中获取文件名
          //获取成功，创建文件，打开文件
          //将头信息从buff中移除，剩下数据进一步匹配
          if(GetFileName(buff,&content_pos))
          {
            CreateFile();
            blen -= content_pos;
            memmove(buff,buff+content_pos,blen);
          }
          else 
          {
            blen -= boundry_pos + _f_boundry.size();
            memmove(buff,buff+boundry_pos,blen);
          }
        }

        std::cerr << buff << std::endl;
        //middle_boundry
        while(1)
        {
          if(MatchBoundry(buff,blen,&boundry_pos) != BOUNDRY_MIDDLE)
            break;
          //1.将boundry之前的数据写入文件，将数据从buff中移除
          //2.关闭文件
          //检测middle中是否又文件名
          
          WriteFile(buff,boundry_pos);
          CloseFile();
          blen -= boundry_pos;
          memmove(buff,buff+boundry_pos,blen); 

          if(GetFileName(buff,&content_pos))
          {
            CreateFile();
            blen -= content_pos;
            memmove(buff,buff+content_pos,blen);
          }
          else 
          {
            if(content_pos == 0)
              break;
            blen -= _m_boundry.size();
            memmove(buff,buff+_m_boundry.size(),blen);
          }
        }

        //last_boundry 

        if(MatchBoundry(buff,blen,&boundry_pos) == BOUNDRY_LAST)
        {
          //1.将boundry之前的数据写入文件
          //2.关闭文件
          //3.上传完毕，退出
          WriteFile(buff,boundry_pos);
          CloseFile();
          return true;
        }
        if(MatchBoundry(buff,blen,&boundry_pos) == BOUNDRY_BAK)
        {
          //1.将类似yuboundry位置之前的数据写入文件
          //2.移除之前的数据
          //剩下的数据不动，重新接收，补全后匹配
          WriteFile(buff,boundry_pos);
          blen -= boundry_pos;
          memmove(buff,buff+boundry_pos,blen);
        }

        if(MatchBoundry(buff,blen,&boundry_pos) == BOUNDRY_NO)
        {
          WriteFile(buff,blen);
          blen = 0;
        }
        tlen +=len;
      }
      return true;
    }

  private:
    int MatchBoundry(char* buff,int blen,int* boundry_pos)
    {
      if(!memcmp(buff,_f_boundry.c_str(),_f_boundry.size())) 
      {
        *boundry_pos = 0;
        return BOUNDRY_FIRST;
      }
      for(int i = 0; i< blen; i++)
      {
        if(!memcpy(buff+i,_m_boundry.c_str(),_m_boundry.size()))
        {
          *boundry_pos = i;
          return BOUNDRY_MIDDLE;
        }
        else if(!memcpy(buff+i,_l_boundry.c_str(),_l_boundry.size()))
        {
          *boundry_pos = i;
          return BOUNDRY_LAST;
        }
        else
        {
          int cmp_len = (blen-i) > _m_boundry.size() ?_m_boundry.size() : (blen-i);
          if(!memcpy(buff+i,_l_boundry.c_str(),cmp_len)) 
          {
            *boundry_pos = i;
            return BOUNDRY_BAK;
          }
          if(!memcpy(buff+i,_m_boundry.c_str(),cmp_len))
          {
            *boundry_pos = i;
            return BOUNDRY_BAK;
          }
        }
        return BOUNDRY_NO;
      }
    }

    bool GetFileName(char* buff,int* content_pos) 
    {
      char* ptr = nullptr;
      ptr = strstr(buff,"\r\n\r\n");
      if(ptr == nullptr)
      {
        *content_pos = 0;
        return false;
      }
      *content_pos = (ptr-buff)+4;
      std::string head;
      head.assign(buff,ptr-buff);

      std::string file_sep = "filename=\"";
      size_t pos = head.find(file_sep);
      if(pos == std::string::npos) 
        return false;

      std::string file;
      file = head.substr(pos+file_sep.size());
      pos = _file_name.find("\"");
      if(pos == std::string::npos) 
        return false;

      file.erase(pos); 
      _file_name = WWWROOT;
      _file_name += "/" + file;

      std::cerr<< _file_name << std::endl;
      return true;
    }

    bool CreateFile()
    {
      _file_fd = open(_file_name.c_str(),O_CREAT | O_WRONLY,0664);
      fprintf(stderr,"create success\n");
      if(_file_fd < 0)
      {
        fprintf(stderr,"open error\n");
        return false;
      }
      return true;
    }

    bool CloseFile()
    {
      if(_file_fd != -1)
      {
        close(_file_fd);
        _file_fd = -1;
      }
      return true;
    }

    bool WriteFile(char* buff,int len)
    {
      if(_file_fd != -1)
      {
        write(_file_fd,buff,len);
        fprintf(stderr,"write success\n");
      }
      return true;
    }

  private:
    int _file_fd;
    std::string _file_name; //文件名
    std::string _f_boundry; //第一个boundry； 
    std::string _m_boundry; //中间的boundry;
    std::string _l_boundry; //最后一个boundry；
    int64_t _content_len; //正文长度
};


