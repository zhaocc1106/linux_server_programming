/**
 * main
 */

#include "tools.hpp"
#include "simple_tcp_server.hpp"
#include "simple_file_down_server.hpp"

#define IP_ADDR "127.0.0.1"
#define PORT 8002
#define RCV_BUF_SIZE 66535
#define TAG "Main"

int main() {
    openlog("Linux_Server", LOG_PID | LOG_CONS, LOG_USER);
    setlogmask(LOG_UPTO(LOG_INFO)); // LOG_UPTO显示小于等于当前priv等级的log

    //是否转成守护进程
    // daemonize(true, true);

    std::thread t1([]() {
        start_simple_tcp_server(IP_ADDR, PORT, RCV_BUF_SIZE);
    });
    std::thread t2([]() {
        // 可以使用 telnet 127.0.0.1 8003 命令测试
        start_simple_file_down_server(IP_ADDR, PORT + 1, "../CMakeLists.txt");
    });

    SYS_LOGI(TAG, "Server started.");

    t1.join();
    t2.join();
}