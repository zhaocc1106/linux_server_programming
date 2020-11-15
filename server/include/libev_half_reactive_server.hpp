/**
* 使用libevent事件驱动 + half-reactive事件处理模式 + 线程池实现及时处理多请求的server
*/

#ifndef SERVER_LIBEV_HALF_REACTIVE_SERVER_HPP
#define SERVER_LIBEV_HALF_REACTIVE_SERVER_HPP

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <errno.h>

#include <arpa/inet.h>
#include <event.h>
#include <evutil.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>

#include "tools.hpp"
#include "thread_safe_queue.hpp"

/************************************************* Marcos *************************************************/

#define LIBEV_ADV_SERVER_TAG "LibevAdvancedServer"

/********************************************* Struct And Enum ********************************************/

/* 主线程与工作线程通信相关--BEGIN */

/* 定义用于主线程与工作线程通信消息类型 */
typedef enum {
    COMM_NEW_CONNECTED, // 有新的连接连接成功
    COMM_TERMINATE, // 关闭所有连接并退出
} CommMsgType;

/* 定义用于主线程与工作线程通信的队列的元素 */
struct CommQueueItem {
    evutil_socket_t fd; // 主线程分派给工作线程的连接好的socket fd

    explicit CommQueueItem(evutil_socket_t fd_) : fd(fd_) {
    }
};

/* 主线程与工作线程通信相关--END */

/* 定义工作线程的上下文，记录当前工作线程一些信息 */
struct CommContex {
    int worker_id; // 工作线程标签
    event_base* ev_base; // 工作线程的事件looper
};

/************************************************* Global *************************************************/

/* 主线程与工作线程通信相关--BEGIN */

std::vector<int> comm_pipes; // 维护所有主线程与工作线程通信的fd
std::vector<zhaocc::ThreadSafeQueue<CommQueueItem>> comm_queues; // 维护所有主线程与工作线程通信的队列

/* 主线程与工作线程通信相关--END */

/************************************************ Functions ***********************************************/

/**
* 工作线程处理客户端发来的信息
* @param be: bufferevent
* @param arg: 额外参数，这里是工作线程上下文
*/
static void client_msg_cb(bufferevent* be, void* arg) {
    auto* ctx = (CommContex*) arg;
    int worker_ind = ctx->worker_id;
    unsigned long th_id = get_thread_id();
    int cli_fd = bufferevent_getfd(be);

    char buf[BUF_SIZE];
    while (true) {
        int n = bufferevent_read(be, buf, BUF_SIZE - 1); // bufferevent 从socket中读取数据
        if (n <= 0) {
            // std::cout << "[Thread-" << th_id << "] bufferevent read error, n: " << n << ", errno: " << errno
            //           << ", errno string: " << strerror(errno) << std::endl;
            // SYS_LOGW(LIBEV_SERVER_TAG, "[Thread-0x%lx] bufferevent read error, n: %d, errno: %d, errno string: %s",
            //          th_id, n, errno, strerror(errno));
            break;
        }

        buf[n] = '\0';
        succ_cli_count++;
        std::cout << "[Worker-" << worker_ind << " Thread-" << th_id << "] Recv from fd(" << cli_fd
                  << "), succ_cli_count: " << succ_cli_count.load() << ", msg len: " << n << ", msg: " << buf
                  << std::endl;
        SYS_LOGI(LIBEV_SERVER_TAG,
                 "[Worker-%d Thread-0x%lx] Recv msg from fd(%d), succ_cli_count: %d, len: %d, msg: %s",
                 worker_ind, th_id, cli_fd, succ_cli_count.load(), n, buf);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟处理请求消耗时间
    bufferevent_write(be, "OK", 2); // 回复一条信息
}

/**
* 工作线程处理socket event事件
* @param be: bufferevent
* @param event: socket event
* @param arg: 额外参数，这里是工作线程上下文
*/
static void client_event_cb(bufferevent* be, short event, void* arg) {
    auto* ctx = (CommContex*) arg;
    unsigned long th_id = get_thread_id();
    int worker_ind = ctx->worker_id;
    int cli_fd = bufferevent_getfd(be);
    // std::cout << "event: " << event << std::endl;
    // SYS_LOGI(LIBEV_SERVER_TAG, "event: 0x%x", event);

    if (event & BEV_EVENT_EOF) {
        std::cout << "[Worker-" << worker_ind << " Thread-" << th_id << "] Remote fd(" << cli_fd << ") disconnect"
                  << std::endl;
        SYS_LOGN(LIBEV_SERVER_TAG, "[Worker-%d Thread-0x%lx] Remote fd(%d) disconnect.", worker_ind, th_id, cli_fd);
        bufferevent_free(be);
    } else if (event & BEV_EVENT_ERROR) {
        std::cout << "[Worker-" << worker_ind << " Thread-" << th_id << "] Remote fd(" << cli_fd << ") error, errno: "
                  << errno << ", errno string: " << strerror(errno) << std::endl;
        SYS_LOGW(LIBEV_SERVER_TAG, "[Worker-%d Thread-0x%lx] Remote fd(%d) error, errno: %d, errno string: %s.",
                 worker_ind, th_id, cli_fd, errno, strerror(errno));
        bufferevent_free(be);
    }
}

/**
* 工作线程收到主线程的信息
* @param fd: fd
* @param event: 事件类型
* @param arg: 额外参数
*/
static void main_msg_cb(evutil_socket_t fd, short event, void* arg) {
    auto* ctx = (CommContex*) arg;
    unsigned long th_id = get_thread_id();
    int worker_ind = ctx->worker_id;

    while (true) {
        uint8_t msg_type;
        int len = recv(fd, &msg_type, 1, 0);

        if (len > 0) {
            /* 处理主线程发来的消息 */
            switch (msg_type) {
            case COMM_NEW_CONNECTED: {
                /* 收到派发过来的新的连接 */
                CommQueueItem cq_msg(-1);
                while (comm_queues[worker_ind].try_pop(cq_msg)) { // 从消息队列中读完所有的消息
                    /* 工作线程监听派发的连接 */
                    std::cout << "[Worker-" << worker_ind << " Thread-" << th_id
                              << "] Received new connection distribution fd: " << cq_msg.fd << std::endl;
                    SYS_LOGN(LIBEV_ADV_SERVER_TAG, "[Worker-%d Thread-0x%lx] Received new connection distribution fd:"
                                                   " %d.", worker_ind, th_id, cq_msg.fd);
                    bufferevent* be = bufferevent_socket_new(ctx->ev_base, cq_msg.fd, BEV_OPT_CLOSE_ON_FREE);
                    bufferevent_setcb(be, client_msg_cb, nullptr, client_event_cb, arg);
                    int ret = bufferevent_enable(be, EV_READ | EV_PERSIST); // 使能event事件
                }
                break;
            }
            case COMM_TERMINATE: {
                /* 收到退出命令 */
                std::cout << "[Worker-" << worker_ind << " Thread-" << th_id << "] Received terminate cmd."
                          << std::endl;
                SYS_LOGN(LIBEV_ADV_SERVER_TAG, "[Worker-%d Thread-0x%lx] Received terminate cmd.", worker_ind, th_id);
                event_base_loopexit(ctx->ev_base, nullptr); // 工作线程looper退出
                return;
            }
            default:
                break;
            }
        } else if (len == 0 || (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            /* 工作线程与主线程沟通的fd被关闭 */
            std::cout << "[Worker-" << worker_ind << " Thread-" << th_id << "] Main thread close communication fd."
                      << std::endl;
            SYS_LOGN(LIBEV_ADV_SERVER_TAG, "[Worker-%d Thread-0x%lx] Main thread close communication fd.",
                     worker_ind, th_id);
            event_base_loopexit(ctx->ev_base, nullptr); // 工作线程looper退出
            break;
        } else {
            break;
        }
    }
}

/**
* 工作线程运行的函数
* @param worker_ind: 工作线程标签
* @param main_fd: 监听主线程事件的fd
*/
static void worker_func(int worker_ind, int main_fd) {
    unsigned long th_id = get_thread_id();
    std::cout << "[Worker-" << worker_ind << " Thread-" << th_id << "] begin running..." << std::endl;
    SYS_LOGN(LIBEV_ADV_SERVER_TAG, "[Worker-%d Thread-0x%lx] begin running...", worker_ind, th_id);

    event_base* base = event_base_new(); // 当前工作线程的事件驱动器

    /* 监听主线程事件 */
    event* main_ev = event_new(nullptr, -1, 0, nullptr, nullptr);
    auto* args = new CommContex; // 创建工作线程的上下文
    args->worker_id = worker_ind;
    args->ev_base = base;
    event_assign(main_ev, base, main_fd, EV_READ | EV_PERSIST, main_msg_cb, args);
    event_add(main_ev, nullptr);

    int ret = event_base_dispatch(base); // 启动工作线程的事件looper

    std::cout << "[Worker-" << worker_ind << " Thread-" << th_id << "] event base looper end, ret: " << ret
              << ", errno str: " << strerror(errno) << std::endl;
    SYS_LOGN(LIBEV_ADV_SERVER_TAG, "[Worker-%d Thread-0x%lx] event base looper  end, ret: %d, errno str: %s",
             worker_ind, th_id, ret, strerror(errno));

    close(main_fd);
    event_free(main_ev);
    delete args;
    event_base_free(base);
}

/**
* 主线程监听连接事件回调
* @param listener: event connection listener.
* @param fd: 连接成功的socket fd
* @param sock: 连接成功的socket addr
* @param socklen: docket addr len
* @param arg: 参数，这里为空
*/
static void conn_listener_cb(evconnlistener* listener, evutil_socket_t fd, struct sockaddr* sock, int socklen,
                             void* arg) {
    auto* cli_addr = (sockaddr_in*) sock;
    std::cout << "[MainThread] Connected with client, fd: " << fd << ", client addr: " << inet_ntoa(cli_addr->sin_addr)
              << ", client port: " << ntohs(cli_addr->sin_port) << std::endl;
    SYS_LOGN(LIBEV_ADV_SERVER_TAG, "[MainThread] Connected with client, fd: %d, client addr: %s, client port: %d.", fd,
             inet_ntoa(cli_addr->sin_addr), ntohs(cli_addr->sin_port));

    evutil_make_socket_nonblocking(fd); // 非阻塞fd

    /* round robin方式派发连接 */
    static int cur_worker_ind = 0;
    int concur = comm_queues.size(); // 当前工作线程数量
    comm_queues[cur_worker_ind].push(CommQueueItem(fd)); // 消息队列放入消息
    uint8_t msg = COMM_NEW_CONNECTED;
    send(comm_pipes[cur_worker_ind], &msg, 1, 0); // 发送派发请求到连接工作线程的fd
    cur_worker_ind++;
    if (cur_worker_ind >= concur) {
        cur_worker_ind = 0; // 新的一轮
    }
}

/**
* 主线程监听信号事件回调
* @param sig: 信号
* @param events: 事件类型
* @param arg: 参数，这里保存主线程的event base
*/
static void signal_event_cb(int sig, short events, void* arg) {
    std::cout << "[Main thread] Received signal: " << sig << std::endl;
    SYS_LOGN(LIBEV_ADV_SERVER_TAG, "[Main thread] Received signal: %d", sig);

    switch (sig) {
    case SIGINT:
        /* 通知所有工作线程停止工作 */
        for (int fd : comm_pipes) {
            uint8_t msg = COMM_TERMINATE;
            send(fd, &msg, 1, 0);
        }

        /* 主线程looper退出 */
        auto* base = (struct event_base*) arg;
        event_base_loopexit(base, nullptr);
        break;
    }
}

/**
* 开启server
* @param ip: ip地址
* @param port: 端口
* @param concur: 线程池并发数量
*/
void start_libev_adv_server(const char* ip, int port, int concur) {
    /* 根据并发数量启动工作线程 */
    boost::asio::thread_pool pool(concur);
    for (int i = 0; i < concur; i++) {
        int fds[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0); // 创建主线程与工作线程的通信通道
        evutil_make_socket_nonblocking(fds[0]); // 非阻塞fd
        evutil_make_socket_nonblocking(fds[1]);
        comm_pipes.push_back(fds[1]); // 保存当前线程的通信fd
        comm_queues.emplace_back(zhaocc::ThreadSafeQueue<CommQueueItem>()); // 保存当前线程的通信队列
        boost::asio::dispatch(pool, [i, fds]() { // 启动工作线程
            worker_func(i, fds[0]);
        });
    }

    /* 设置tcp/ipv4的socket地址 */
    sockaddr_in srv_addr{};
    bzero(&srv_addr, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET; // 协议族设置tcp/ipv4
    inet_pton(AF_INET, ip, &srv_addr.sin_addr); // 将ip地址转换成网络序并且保存在结构体中
    srv_addr.sin_port = htons(port); // 将端口号从host字节序转换成网络字节序并且保存在结构体中

    event_base* base = event_base_new();

    /* 注册信号事件 */
    struct event* sig_listener = evsignal_new(base, SIGINT, signal_event_cb, base);
    event_add(sig_listener, nullptr);

    /* 注册socket连接事件 */
    evconnlistener* con_listener = evconnlistener_new_bind(base, conn_listener_cb, base,
                                                           LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, 65535,
                                                           (sockaddr*) &srv_addr, sizeof(sockaddr_in));
    int ret = event_base_dispatch(base);
    std::cout << "[Main thread] event base looper end, ret: " << ret << ", errno str: " << strerror(errno) << std::endl;
    SYS_LOGN(LIBEV_ADV_SERVER_TAG, "[Main thread] event base looper end, ret: %d, errno str: %s.", ret,
             strerror(errno));

    evconnlistener_free(con_listener);
    event_free(sig_listener);
    event_base_free(base);
    pool.join(); // 等待工作线程退出
}

#endif //SERVER_LIBEV_HALF_REACTIVE_SERVER_HPP
