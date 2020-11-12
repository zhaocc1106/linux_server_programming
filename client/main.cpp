/**
 * main
 * 测试方式：client [ip] [port] [max open fd count] [request count] [thread count of pool]
 */

#include <sys/resource.h>

#include "simple_tcp_client.hpp"
#include "libevent_no_block_client.hpp"

#define SEND_BUF_SIZE 2304

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Used as: client [ip] [port] [max open fd count] [request count] [thread count of pool]"
                  << std::endl;
        return 1;
    }

    char* ip = argv[1];
    short port = (short) atoi(argv[2]);
    unsigned long max_open_fd = 4096;
    int req_count = 1000;
    int concur = std::thread::hardware_concurrency();

    if (argc >= 4) {
        max_open_fd = atoi(argv[3]); // 最大打开的fd个数
    }

    if (argc >= 5) {
        req_count = atoi(argv[4]); // 线程池并发数量
    }

    if (argc >= 6) {
        concur = atoi(argv[5]); // 线程池并发数量
    }

    struct rlimit limit{max_open_fd, max_open_fd};
    setrlimit(RLIMIT_OFILE, &limit);
    getrlimit(RLIMIT_OFILE, &limit);
    std::cout << "FD limit, soft: " << limit.rlim_cur << ", hard: " << limit.rlim_max << std::endl;

    // start_simple_tcp_client(IP_ADDR, PORT, SEND_BUF_SIZE);

    start_multi_client(ip, port, req_count, concur);
}