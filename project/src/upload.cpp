#include "HttpServer.hpp"

int main()
{
  Upload upload;
  if(upload.InitUpload() == false)
    return 0;
  std::string rsp_body;

  std::cerr << "nihao" << std::endl;
  if(upload.ProcessUpLoad() == false)
  {
    rsp_body = "<html><body><h1>Failed!!!</h1></body></html>";
  }
  else 
  {
    rsp_body = "<html><body><h1>Success!!!</h1></body></html>";
  }
  std::cout << rsp_body << std::endl;
  fflush(stdout);
  return 0;

}
