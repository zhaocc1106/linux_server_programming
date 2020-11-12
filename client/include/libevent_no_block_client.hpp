/**
* 使用libevent实现非阻塞多客户端
*/

#ifndef CLIENT_LIBEVENT_NO_BLOCK_CLIENT_HPP
#define CLIENT_LIBEVENT_NO_BLOCK_CLIENT_HPP

#include <iostream>
#include <chrono>
#include <atomic>
#include <cstring>
#include <time.h>
#include <unistd.h>

#include <event.h>
#include <event2/bufferevent.h>
#include <boost/asio.hpp>

#define BUF_SIZE 4096

static int total_req_count = 0; // 总客户端请求数量
static std::atomic<int> comp_req_count(0); // 成功的客户端请求数量
int64_t begin_time_ms; // 开始时间

static int connect_to_server(bufferevent* bev, const char* ip, short port) {
    /* 设置tcp/ipv4的socket地址 */
    sockaddr_in srv_addr{};
    bzero(&srv_addr, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET; // 协议族设置tcp/ipv4
    inet_pton(AF_INET, ip, &srv_addr.sin_addr); // 将ip地址转换成网络序并且保存在结构体中
    srv_addr.sin_port = htons(port); // 将端口号从host字节序转换成网络字节序并且保存在结构体中

    return bufferevent_socket_connect(bev, (struct sockaddr*) &srv_addr, sizeof(srv_addr)); // 使用bufferevent创建socket并连接
}

static void server_msg_cb(struct bufferevent* bev, void* arg) {
    char msg[BUF_SIZE];

    size_t len = bufferevent_read(bev, msg, BUF_SIZE - 1);
    msg[len] = '\0';

    comp_req_count++;
    std::cout << "comp_req_count: " << comp_req_count.load() << ", fd(" << bufferevent_getfd(bev) << ") received msg: "
              << msg << std::endl;
    if (total_req_count == comp_req_count.load()) {
        int64_t end_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        std::cout << "All client request(" << total_req_count << ") complete with cost " << end_time_ms - begin_time_ms
                  << " ms" << std::endl;
    }
    // bufferevent_free(bev);
}

static void writable_cb(struct bufferevent* bev, void* arg) {
    std::cout << "fd(" << bufferevent_getfd(bev) << ") writable" << std::endl;
}

static void event_cb(struct bufferevent* bev, short event, void* arg) {
    int sock_fd = bufferevent_getfd(bev);
    if (event & BEV_EVENT_EOF) {
        std::cout << "fd(" << sock_fd << ") disconnect." << std::endl;
        goto ERROR;
    } else if (event & BEV_EVENT_ERROR) {
        std::cout << "fd(" << sock_fd << ") error, errno str: " << strerror(errno) << std::endl;
        goto ERROR;
    } else if (event & BEV_EVENT_CONNECTED) {
        std::cout << "fd(" << sock_fd << ") connected to server." << std::endl;
        bufferevent_write(bev, "Hello!", 6);
    }
    return;

    ERROR:
    bufferevent_free(bev);
}

static void start_clients_func(const char* ip, short port, int sub_cli_count) {
    struct event_base* base = event_base_new();

    for (int i = 0; i < sub_cli_count; i++) {
        struct bufferevent* bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
        if (connect_to_server(bev, ip, port) == 0) {
            int sock_fd = bufferevent_getfd(bev);
            evutil_make_socket_nonblocking(sock_fd);

            /* 尝试修改tcp 发送缓冲大小 */
            int send_buf_size = 1024; // 实际为1024 * 2
            int len = sizeof(send_buf_size);
            setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));
            getsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &send_buf_size, (socklen_t*) &len);
            // std::cout << "The tcp send buffer size: " << send_buf_size << std::endl;

            bufferevent_setcb(bev, server_msg_cb, nullptr, event_cb, base);
            bufferevent_enable(bev, EV_READ | EV_PERSIST);
        } else {
            std::cout << "Socket try connect failed, errno str: " << strerror(errno) << std::endl;
            bufferevent_free(bev);
        }
    }

    event_base_dispatch(base);
    event_base_free(base);
}

/**
* 开启多个客户端，尝试去连接服务器并且通信
* @param ip: ip地址
* @param port: 端口号
* @param client_count: 客户端数量
* @param thread_count: 线程池容量
*/
void start_multi_client(const char* ip, short port, int client_count, int thread_count) {
    begin_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    total_req_count = client_count;

    boost::asio::thread_pool pool(thread_count);
    int sub_cli_count = client_count / thread_count;
    int res_cli_count = client_count % thread_count;

    for (int i = 0; i < thread_count; i++) {
        int count = sub_cli_count;
        if (res_cli_count > 0) {
            count += 1;
            res_cli_count--;
        }

        boost::asio::dispatch(pool, [ip, port, count]() {
            start_clients_func(ip, port, count);
        });
    }

    pool.join();
}

#endif //CLIENT_LIBEVENT_NO_BLOCK_CLIENT_HPP
