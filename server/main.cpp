/**
 * main
 */

#include "tools.hpp"
#include "simple_tcp_server.hpp"
#include "simple_file_down_server.hpp"

#define IP_ADDR "127.0.0.1"
#define PORT 8002
#define RCV_BUF_SIZE 66535

int main() {
    // daemonize(true, true); //是否转成守护进程

    std::thread t1([]() {
        start_simple_tcp_server(IP_ADDR, PORT, RCV_BUF_SIZE);
    });
    std::thread t2([]() {
        start_simple_file_down_server(IP_ADDR, PORT + 1, "../CMakeLists.txt");
    });

    t1.join();
    t2.join();
}