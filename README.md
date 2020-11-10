# linux_server_programming
linux服务器编程

## server
[simple_tcp_server.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/simple_tcp_server.hpp)：简单的tcp server，回顾常用的socket api，以及socket创建，监听，接受，读写。<br>
[simple_file_down_server.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/simple_file_down_server.hpp)：一个简单的文件下载服务器，使用sendfile函数实现零拷贝（用户空间和内核空间）。<br>
[tools.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/tools.hpp)：常用工具，例如进程守护化方法。<br>
[multi_connection_server.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/multi_connection_server.hpp)：使用select，poll，epoll（et与lt触发模式，并且reactor I/O模型将任务放到分发线程池中提升并发）多路复用机制实现支持多客户端连接的服务器。<br>

## client
[simple_tcp_client.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/client/include/simple_tcp_client.hpp)：简单的tcp client，回顾常用的socket api，以及socket创建，连接，读写。<br>
