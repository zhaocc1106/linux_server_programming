/**
 * 一个简单的文件下载服务器，使用sendfile函数实现零拷贝（用户空间和内核空间），该io函数非常适合用于从服务器下载文件。
 */

#ifndef SERVER_SIMPLE_FILE_DOWN_SERVER_HPP
#define SERVER_SIMPLE_FILE_DOWN_SERVER_HPP

#include <iostream>
#include <cassert>
#include <fcntl.h>
#include <unistd.h>
#include <error.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>

#include "tools.hpp"

#define FILE_DOWN_TAG "FileDownServer"

/**
 * Start simple file downloader server.
 * @param ip: Server ip address.
 * @param port: Server port.
 * @param file_path: The file path.
 * @return status.
 */
int start_simple_file_down_server(const char* ip, int port, const char* file_path) {
    /* 设置tcp/ipv4的socket地址 */
    sockaddr_in srv_addr{};
    bzero(&srv_addr, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET; // 协议族设置tcp/ipv4
    inet_pton(AF_INET, ip, &srv_addr.sin_addr); // 将ip地址转换成网络序并且保存在结构体中
    srv_addr.sin_port = htons(port); // 将端口号从host字节序转换成网络字节序并且保存在结构体中

    int srv_fd = socket(PF_INET, SOCK_STREAM, 0); // 创建一个tcp协议族的socket
    assert(srv_fd >= 0);

    /* 设置SO_REUSEADDR标志 */
    int reuse = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* socket bind地址 */
    int ret = bind(srv_fd, (sockaddr*) &srv_addr, sizeof(srv_addr));
    assert(ret != -1);

    /* listen */
    ret = listen(srv_fd, 1024);
    assert(ret != -1);
    std::cout << "Server begin listening..." << std::endl;
    SYS_LOGI(FILE_DOWN_TAG, "Server begin listening...");

    sockaddr_in cli_addr{}; // 用于保存client socket地址
    bzero(&cli_addr, sizeof(cli_addr));

    /* accept */
    socklen_t cli_addr_len = sizeof(cli_addr);
    int cli_fd = accept(srv_fd, (sockaddr*) &cli_addr, &cli_addr_len);

    /* send file */
    if (cli_fd < 0) {
        std::cout << "Accept failed with error: " << errno << ", error str: " << strerror(errno) << std::endl;
        SYS_LOGW(FILE_DOWN_TAG, "Accept failed with error: %d, error str: %s.", errno, strerror(errno));
        return 1;
    } else {
        /* 打开文件 */
        int file_fd = open(file_path, O_RDONLY);
        assert(file_fd > 0);
        struct stat file_stat{};
        fstat(file_fd, &file_stat);

        // 使用sendfile函数实现从文件fd读取到客户端fd中，并且零拷贝（用户空间和内核空间）。
        ssize_t count = sendfile(cli_fd, // 必须是一个socket fd
                                 file_fd, // 必须是一个文件fd
                                 nullptr,
                                 file_stat.st_size);
        std::cout << "Send file with bytes: " << file_stat.st_size << ", send successfully bytes: " << count
                  << std::endl;
        SYS_LOGI(FILE_DOWN_TAG, "Send file with bytes: %ld, send successfully bytes: %zd", file_stat.st_size, count);

        close(file_fd);
        close(cli_fd);
    }

    close(srv_fd);
}

#endif //SERVER_SIMPLE_FILE_DOWN_SERVER_HPP
