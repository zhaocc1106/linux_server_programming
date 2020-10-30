/**
 * 一个简单的tcp server
 */

#ifndef SERVER_SIMPLE_TCP_SERVER_HPP
#define SERVER_SIMPLE_TCP_SERVER_HPP

#include <iostream>
#include <unistd.h>
#include <error.h>
#include <string.h>
#include <cassert>
#include <thread>
#include <chrono>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/**
 * Start simple tcp server.
 * @param ip: Server ip address.
 * @param port: Server port.
 * @param recv_buf_size: The tcp receive buffer size.
 * @return status.
 */
int start_simple_tcp_server(const char* ip, int port, int recv_buf_size = 1152) {
    /* 设置tcp/ipv4的socket地址 */
    sockaddr_in srv_addr;
    bzero(&srv_addr, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET; // 协议族设置tcp/ipv4
    inet_pton(AF_INET, ip, &srv_addr.sin_addr); // 将ip地址转换成网络序并且保存在结构体中
    srv_addr.sin_port = htons(port); // 将端口号从host字节序转换成网络字节序并且保存在结构体中

    int srv_fd = socket(PF_INET, SOCK_STREAM, 0); // 创建一个tcp协议族的socket
    assert(srv_fd >= 0);

    /* 设置SO_REUSEADDR标志 */
    int reuse = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* 设置一下接收缓冲区大小 */
    int len = sizeof(recv_buf_size);
    setsockopt(srv_fd, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size));
    getsockopt(srv_fd, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, (socklen_t*) &len);
    std::cout << "The tcp receive buffer size: " << recv_buf_size << std::endl;

    /* socket bind地址 */
    int ret = bind(srv_fd, (sockaddr*) &srv_addr, sizeof(srv_addr));
    assert(ret != -1);

    /* listen */
    ret = listen(srv_fd, 1024);
    assert(ret != -1);
    std::cout << "Server begin listening..." << std::endl;

    sockaddr_in cli_addr; // 用于保存client socket地址
    bzero(&cli_addr, sizeof(srv_addr));

    /* accept */
    socklen_t cli_addr_len = sizeof(cli_addr);
    int cli_fd = accept(srv_fd, (sockaddr*) &cli_addr, &cli_addr_len);

    /* recv */
    if (cli_fd < 0) {
        std::cout << "Accept failed with error: " << errno << ", error str: " << strerror(errno) << std::endl;
        return 1;
    } else {
        char buf[BUFSIZ];
        memset(buf, 0, sizeof(buf));
        while (true) {
            ssize_t n = recv(cli_fd, buf, BUFSIZ - 1, 0);
            if (n > 0) { // 收到有效msg
                std::cout << "Recv msg len: " << n << ", msg: " << buf << std::endl;
                memset(buf, 0, sizeof(buf));
            } else if (n < 0) { // 收包出错
                std::cout << "Recv failed with error num: " << errno << ", error str: " << strerror(errno) << std::endl;
                exit(1);
            } else { // 对方断开了链接
                std::cout << "Remote disconnect" << std::endl;
                close(cli_fd);
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
        close(srv_fd);
        return 0;
    }
}

#endif //SERVER_SIMPLE_TCP_SERVER_HPP
