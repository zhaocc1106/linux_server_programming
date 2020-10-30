/**
 * 一个简单的tcp client
 */

#ifndef CLIENT_SIMPLE_TCP_CLIENT_HPP
#define CLIENT_SIMPLE_TCP_CLIENT_HPP

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
 * Start simple tcp client.
 * @param ip: Server ip address.
 * @param port: Server port.
 * @param send_buf_size: The tcp send buffer size.
 * @return status.
 */
int start_simple_tcp_client(const char* ip, int port, int send_buf_size = 2304) {
    /* 设置tcp/ipv4的socket地址 */
    sockaddr_in srv_addr;
    bzero(&srv_addr, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET; // 协议族设置tcp/ipv4
    inet_pton(AF_INET, ip, &srv_addr.sin_addr); // 将ip地址转换成网络序并且保存在结构体中
    srv_addr.sin_port = htons(port); // 将端口号从host字节序转换成网络字节序并且保存在结构体中

    int srv_fd = socket(PF_INET, SOCK_STREAM, 0); // 创建一个tcp协议族的socket
    assert(srv_fd >= 0);

    /* 尝试修改tcp 发送缓冲大小 */
    int len = sizeof(send_buf_size);
    setsockopt(srv_fd, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));
    getsockopt(srv_fd, SOL_SOCKET, SO_SNDBUF, &send_buf_size, (socklen_t*) &len);
    std::cout << "The tcp send buffer size: " << send_buf_size << std::endl;

    /* connect */
    if (connect(srv_fd, (sockaddr*) &srv_addr, sizeof(srv_addr)) != -1) {

        /* send */
        char buff[BUFSIZ];
        memset(buff, 'a', sizeof(buff));
        int len = send(srv_fd, buff, BUFSIZ, 0);
        if (len > 0) {
            std::cout << "Send successfully with len: " << len << std::endl;
        } else {
            std::cout << "Send failed with error number: " << errno << ", error str: " << strerror(errno) << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
        close(srv_fd);
        return 0;
    }
}

#endif //CLIENT_SIMPLE_TCP_CLIENT_HPP
