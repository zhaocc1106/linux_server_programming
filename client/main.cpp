/**
 * main
 */

#include "simple_tcp_client.hpp"

#define IP_ADDR "127.0.0.1"
#define PORT 8002
#define SEND_BUF_SIZE 2304

int main() {
    start_simple_tcp_client(IP_ADDR, PORT, SEND_BUF_SIZE);
}