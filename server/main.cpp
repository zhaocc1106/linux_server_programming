/**
 * main
 */

#include "tools.hpp"
#include "simple_tcp_server.hpp"
#include "simple_file_down_server.hpp"
#include "multi_connection_server.hpp"
#include "libev_multi_con_server.hpp"

#define IP_ADDR "127.0.0.1"
#define BEGIN_PORT 8002
#define RCV_BUF_SIZE 66535
#define TAG "Main"

int main() {
    openlog("Linux_Server", LOG_PID | LOG_CONS, LOG_USER);
    setlogmask(LOG_UPTO(LOG_INFO)); // LOG_UPTO显示小于等于当前priv等级的log

    //是否转成守护进程
    // daemonize(true, true);

    /*
    start_simple_tcp_server(IP_ADDR, BEGIN_PORT, RCV_BUF_SIZE);
    start_simple_file_down_server(IP_ADDR, BEGIN_PORT + 1, "../CMakeLists.txt");
    start_multi_con_server(IP_ADDR, BEGIN_PORT + 2, MT_SELECT);
    start_multi_con_server(IP_ADDR, BEGIN_PORT + 3, MT_POLL);
    start_multi_con_server(IP_ADDR,
                           BEGIN_PORT + 4,
                           MT_EPOLL, // Epoll多路复用机制类型
                           EP_ET); // 如果是Epoll，则需要指定trigger类型，其他机制可以忽略
    */
    start_libev_multi_server(IP_ADDR, BEGIN_PORT + 5, SIMPLE);

    SYS_LOGI(TAG, "Server started.");
}