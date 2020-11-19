/**
 * main
 * 测试方式：server [ip] [port] [max open fd] [thread count of pool]
 */

#include <sys/resource.h>

#include "tools.hpp"
// #include "simple_tcp_server.hpp"
// #include "simple_file_down_server.hpp"
// #include "multi_connection_server.hpp"
// #include "libev_multi_con_server.hpp"
#include "libev_half_sync_reactive_thread_pool_server.hpp"
// #include "epoll_half_sync_reactive_proc_pool_server.hpp"

#define RCV_BUF_SIZE 66535
#define TAG "Main"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Used as: server [ip] [port] [max open fd] [thread count of pool]" << std::endl;
        SYS_LOGW(TAG, "Used as: server [ip] [port] [max open fd] [thread count of pool]");
        return 1;
    }

    char* ip = argv[1];
    short port = (short) atoi(argv[2]);
    unsigned long max_open_fd = 4096;
    int concur = std::thread::hardware_concurrency();

    if (argc >= 4) {
        max_open_fd = atoi(argv[3]); // 最大打开的fd个数
    }

    if (argc >= 5) {
        concur = atoi(argv[4]); // 线程池并发数量
    }

    openlog("Linux_Server", LOG_PID | LOG_CONS, LOG_USER);
    setlogmask(LOG_UPTO(LOG_INFO)); // LOG_UPTO显示小于等于当前priv等级的log

    struct rlimit limit{max_open_fd, max_open_fd};
    setrlimit(RLIMIT_OFILE, &limit);
    getrlimit(RLIMIT_OFILE, &limit);
    std::cout << "FD limit, soft: " << limit.rlim_cur << ", hard: " << limit.rlim_max << std::endl;
    SYS_LOGI(TAG, "FD limit, soft: %lu, hard: %lu.", limit.rlim_cur, limit.rlim_max);

    //是否转成守护进程
    // daemonize(true, true);

    // start_simple_tcp_server(ip, port, RCV_BUF_SIZE);
    // start_simple_file_down_server(ip, port, "../CMakeLists.txt");
    // start_multi_con_server(ip, port, MT_SELECT);
    // start_multi_con_server(ip, port, MT_POLL);
    // start_multi_con_server(ip,
    //                        port,
    //                        MT_EPOLL, // Epoll多路复用机制类型
    //                        EP_ET); // 如果是Epoll，则需要指定trigger类型，其他机制可以忽略
    // start_libev_multi_server(ip, port);
    start_libev_adv_server(ip, port, concur);
    // start_process_pool_server(ip, port, concur);
}