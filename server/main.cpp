/**
 * main
 */

#include "simple_tcp_server.hpp"

#define IP_ADDR "127.0.0.1"
#define PORT 8002
#define RCV_BUF_SIZE 66535

int main() {
    start_simple_tcp_server(IP_ADDR, PORT, RCV_BUF_SIZE);
}