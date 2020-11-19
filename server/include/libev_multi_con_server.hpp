/**
* 使用libevent事件驱动，单线程处理事件实现接受多个客户端连接的服务器
*/

#ifndef SERVER_LIBEV_MULTI_CON_SERVER_HPP
#define SERVER_LIBEV_MULTI_CON_SERVER_HPP

#include <iostream>
#include <thread>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <csignal>

#include <arpa/inet.h>
#include <event.h>
#include <evutil.h>

#include "tools.hpp"

#define LIBEV_SERVER_TAG "LibevMultiConnServer"

static std::atomic<int> succ_cli_count(0); // 完成收消息的客户端数量

/* init server */
static int tcp_server_init(const char* ip, int port) {
    /* 设置tcp/ipv4的socket地址 */
    sockaddr_in srv_addr{};
    bzero(&srv_addr, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET; // 协议族设置tcp/ipv4
    inet_pton(AF_INET, ip, &srv_addr.sin_addr); // 将ip地址转换成网络序并且保存在结构体中
    srv_addr.sin_port = htons(port); // 将端口号从host字节序转换成网络字节序并且保存在结构体中

    int srv_fd = socket(PF_INET, SOCK_STREAM, 0); // 创建一个tcp协议族的socket
    assert(srv_fd >= 0);

    /* 设置socket addr reuseable标志 */
    evutil_make_listen_socket_reuseable(srv_fd);

    /* socket bind地址 */
    int ret = bind(srv_fd, (sockaddr*) &srv_addr, sizeof(srv_addr));
    if (ret == -1) {
        goto ERROR;
    }

    /* listen */
    ret = listen(srv_fd, 65535);
    if (ret == -1) {
        goto ERROR;
    }
    std::cout << "Server begin listening..." << std::endl;
    SYS_LOGI(LIBEV_SERVER_TAG, "Server begin listening...");

    /* 设置socket非阻塞 */
    evutil_make_socket_nonblocking(srv_fd);

    return srv_fd;

    ERROR:
    evutil_closesocket(srv_fd);
    return -1;
}

/* 客戶端消息事件回调 */
static void msg_cb(int cli_fd, short events, void* arg) {
    char buf[BUF_SIZE];
    unsigned long th_id = get_thread_id();

    while (true) {
        ssize_t n = recv(cli_fd, buf, BUF_SIZE - 1, 0);

        if (n > 0) { // 收到有效msg
            buf[n] = '\0';
            std::cout << "[Thread-" << th_id << "] Recv from fd(" << cli_fd << ") msg len: " << n
                      << ", msg: " << buf << std::endl;
            SYS_LOGI(LIBEV_SERVER_TAG, "[Thread-0x%lx] Recv msg from fd(%d), len: %zd, msg: %s", th_id, cli_fd, n, buf);
        } else if (n < 0) { // 收包出错
            std::cout << "[Thread-" << th_id << "] Recv from fd(" << cli_fd << ") failed with error num: " << errno
                      << ", error str: " << strerror(errno) << std::endl;
            SYS_LOGW(LIBEV_SERVER_TAG, "[Thread-0x%lx] Recv from fd(%d) failed with error num: %d, error str: %s.",
                     th_id, cli_fd, errno, strerror(errno));
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                goto ERROR;
            } else {
                break;
            }
        } else { // 对方断开了链接
            std::cout << "[Thread-" << th_id << "] Remote fd(" << cli_fd << ") disconnect" << std::endl;
            SYS_LOGN(LIBEV_SERVER_TAG, "[Thread-0x%lx] Remote fd(%d) disconnect.", th_id, cli_fd);
            goto ERROR;
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟处理请求消耗时间
    send(cli_fd, "OK\r\n", 4, 0); // 回复一个消息
    return;

    ERROR:
    /* 对方断开，注销事件，关闭fd */
    auto ev = (struct event*) arg;
    event_free(ev);
    close(cli_fd);
}

/* 信号消息事件回调 */
static void signal_cb(int sig, short events, void* arg) {
    std::cout << "Received signal: " << sig << std::endl;
    SYS_LOGN(LIBEV_SERVER_TAG, "Received signal: %d", sig);

    switch (sig) {
    case SIGINT:
        auto* base = (struct event_base*) arg;
        event_base_loopexit(base, nullptr);
        break;
    }
}

/* bufferevent处理客户端信息 */
static void be_msg_cb(bufferevent* be, void* arg) {
    char buf[BUF_SIZE];
    int cli_fd = bufferevent_getfd(be);
    unsigned long th_id = get_thread_id();

    while (true) {
        int n = bufferevent_read(be, buf, BUF_SIZE - 1); // bufferevent 从socket中读取数据
        if (n <= 0) {
            // std::cout << "[Thread-" << th_id << "] bufferevent read error, n: " << n << ", errno: " << errno
            //           << ", errno string: " << strerror(errno) << std::endl;
            // SYS_LOGW(LIBEV_SERVER_TAG, "[Thread-0x%lx] bufferevent read error, n: %d, errno: %d, errno string: %s",
            //          th_id, n, errno, strerror(errno));
            break;
        }

        buf[n] = '\0';
        succ_cli_count++;
        std::cout << "[Thread-" << th_id << "] Recv from fd(" << cli_fd << "), succ_cli_count: "
                  << succ_cli_count.load() << ", msg len: " << n << ", msg: " << buf << std::endl;
        SYS_LOGI(LIBEV_SERVER_TAG, "[Thread-0x%lx] Recv msg from fd(%d), succ_cli_count: %d, len: %d, msg: %s",
                 th_id,
                 cli_fd, succ_cli_count.load(), n, buf);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟处理请求消耗时间
    bufferevent_write(be, "OK", 2); // 回复一条信息
}

/* bufferevent处理socket事件 */
static void be_event_cb(bufferevent* be, short event, void* arg) {
    int cli_fd = bufferevent_getfd(be);
    unsigned long th_id = get_thread_id();
    // std::cout << "event: " << event << std::endl;
    // SYS_LOGI(LIBEV_SERVER_TAG, "event: 0x%x", event);

    if (event & BEV_EVENT_EOF) {
        std::cout << "[Thread-" << th_id << "] Remote fd(" << cli_fd << ") disconnect" << std::endl;
        SYS_LOGN(LIBEV_SERVER_TAG, "[Thread-0x%lx] Remote fd(%d) disconnect.", th_id, cli_fd);
        bufferevent_free(be);
    } else if (event & BEV_EVENT_ERROR) {
        std::cout << "[Thread-" << th_id << "] Remote fd(" << cli_fd << ") error, errno: " << errno
                  << ", errno string: " << strerror(errno)
                  << std::endl;
        SYS_LOGW(LIBEV_SERVER_TAG, "[Thread-0x%lx] Remote fd(%d) error, errno: %d, errno string: %s.", th_id,
                 cli_fd, errno, strerror(errno));
        bufferevent_free(be);
    }
}

/* 客户端连接事件回调 */
static void accept_cb(int srv_fd, short events, void* arg) {
    struct sockaddr_in cli_addr{};
    socklen_t len = sizeof(sockaddr_in);

    evutil_socket_t cli_fd = accept(srv_fd, (sockaddr*) &cli_addr, &len);
    if (cli_fd < 0) {
        std::cout << "Accept failed with errno: " << errno << ", errno str: " << strerror(errno) << std::endl;
        SYS_LOGW(LIBEV_SERVER_TAG, "Accept failed with errno: %d, errno str: %s.", errno, strerror(errno));
        return;
    }

    evutil_make_socket_nonblocking(cli_fd); // 设置非阻塞
    std::cout << "Connected with client, fd: " << cli_fd << ", client addr: " << inet_ntoa(cli_addr.sin_addr)
              << ", client port: " << ntohs(cli_addr.sin_port) << std::endl;
    SYS_LOGN(LIBEV_SERVER_TAG, "Connected with client, fd: %d, client addr: %s, client port: %d.", cli_fd,
             inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

    /* 注册客户端信息事件 */
    auto base = (struct event_base*) arg;
    /* 基础函数注册 */
    // struct event* ev = event_new(nullptr, -1, 0, nullptr, nullptr); // 仅创建
    // event_assign(ev, base, cli_fd, EV_READ | EV_PERSIST, msg_cb, ev); // assign event
    // event_add(ev, nullptr);

    /* bufferevent高阶函数注册 */
    bufferevent* be = bufferevent_socket_new(base, cli_fd, BEV_OPT_CLOSE_ON_FREE); // 创建bufferevent socket
    bufferevent_setcb(be, be_msg_cb, nullptr, be_event_cb, arg); // 设置bufferevent read，event回调与参数
    bufferevent_enable(be, EV_READ | EV_PERSIST); // 使能event事件
}

/**
* 开启server
* @param ip: ip地址
* @param port: 端口
*/
void start_libev_multi_server(const char* ip, int port) {
    int srv_fd = tcp_server_init(ip, port);
    if (srv_fd == -1) {
        std::cout << "Tcp server init failed." << std::endl;
        SYS_LOGE(LIBEV_SERVER_TAG, "Tcp server init failed.");
        return;
    }

    struct event_base* base = event_base_new(); // 创建event_base

    /* 注册信号事件 */
    struct event* sig_listener = evsignal_new(base, SIGINT, signal_cb, base);
    event_add(sig_listener, nullptr);

    /* 注册客户端连接事件 */
    struct event* accept_listener = event_new(base, srv_fd, EV_READ | EV_PERSIST, accept_cb, base);
    event_add(accept_listener, nullptr);

    int ret = event_base_dispatch(base); // 运行event base looper
    std::cout << "Event looper stopped ret: " << ret << ", errno str: " << strerror(errno) << std::endl;
    SYS_LOGN(LIBEV_SERVER_TAG, "Event looper stopped ret: %d, errno str: %s.", ret, strerror(errno));

    event_base_free(base);

    close(srv_fd);
}

#endif //SERVER_LIBEV_MULTI_CON_SERVER_HPP
