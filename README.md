# linux_server_programming
linux服务器编程

## server
[tools.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/tools.hpp)：常用工具，例如进程守护化方法。<br>
[thread_safe_queue.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/thread_safe_queue.hpp)：多线程安全队列。<br>
[simple_tcp_server.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/simple_tcp_server.hpp)：简单的tcp server，回顾常用的socket api，以及socket创建，监听，接受，读写。<br>
[simple_file_down_server.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/simple_file_down_server.hpp)：一个简单的文件下载服务器，使用sendfile函数实现零拷贝（用户空间和内核空间）。<br>
[multi_connection_server.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/multi_connection_server.hpp)：使用select，poll，epoll（et与lt触发模式，并且reactor I/O模型将任务放到分发线程池中提升并发）多路复用机制实现支持多客户端连接的服务器。<br>
[libev_multi_con_server.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/libev_multi_con_server.hpp)：使用libevent事件驱动和单线程实现支持多客户端连接的服务器。<br>
[libev_half_sync_reactive_thread_pool_server.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/libev_half_sync_reactive_thread_pool_server.hpp)：使用libevent事件驱动 + half-sync/half-reactive事件处理模式 + 线程池实现高速处理多请求的server。<br>
[epoll_half_sync_reactive_proc_pool_server.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/server/include/epoll_half_sync_reactive_proc_pool_server.hpp)：使用epoll多路复用 + half-sync/half-reactive事件处理模式 + 进程池实现及时处理多请求的server。<br>

## client
[simple_tcp_client.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/client/include/simple_tcp_client.hpp)：简单的tcp client，回顾常用的socket api，以及socket创建，连接，读写。<br>
[libevent_no_block_client.hpp](https://github.com/zhaocc1106/linux_server_programming/blob/main/client/include/libevent_no_block_client.hpp)：使用libevent事件驱动实现非阻塞多客户端请求。
