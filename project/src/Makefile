all:HttpServer upload

HttpServer:HttpServer.cpp 
	g++ -o $@ $^ -pthread -std=c++11 

upload:upload.cpp 
	g++ -o $@ $^ -pthread -std=c++11 

.PHONY:clean
clean:
	rm -f HttpServer upload
