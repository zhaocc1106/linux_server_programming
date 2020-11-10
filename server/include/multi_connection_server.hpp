/**
* 通过I/O复用技术(select, poll, epoll)实现能够处理多个客户端的服务器。
* 其中epoll机制使用两种trigger方式实现，并且将任务放到线程池中去做。
*/

#ifndef SERVER_MULTI_CONNECTION_SERVER_HPP
#define SERVER_MULTI_CONNECTION_SERVER_HPP

#include <iostream>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <boost/asio.hpp>
#include <unistd.h>
#include <string.h>
#include <sys/un.h>
#include <stddef.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "tools.hpp"

#define MULT_CON_SRV_TAG "MultiConnServer"
#define SELECT_FDS_LEN 128 // select fd 集合最大个数
#define POLL_FDS_LEN 1024 // poll fd 集合最大个数
#define EPOLL_FDS_LEN 1024 // epoll fd 集合最大个数
#define BUF_SIZE 10

/* 多路复用的类型 */
typedef enum {
    MT_SELECT, // select多路复用
    MT_POLL, // poll多路复用
    MT_EPOLL // epoll多路复用
} MultiplexType;

/* epoll trigger类型 */
typedef enum {
    EP_LT, // level trigger
    EP_ET // edge trigger
} EpollTriggerType;

/* epoll相关全局变量 */
static bool running = true; // server是否正在运行
static int sig_pipe_fd[2]; // 用于信号处理函数将信号传送到main线程的io事件中来处理
static int epfd = -1; // epoll fd
static std::unordered_set<int> ep_fds; // 保存所有epoll监控的fd
static std::mutex epfd_mutex; // 保护多线程处理epfd与ep_fds

/* 信号处理函数 */
static void sig_handler(int sig) {
    // std::cout << "Received signal: " << sig << std::endl;
    // SYS_LOGW(MULT_CON_SRV_TAG, "Received signal: %d", sig);
    int save_errno = errno; // 信号处理完后恢复errno
    int8_t sig_ = (uint8_t) sig;
    if (sig_pipe_fd[1] != -1) {
        send(sig_pipe_fd[1], (char*) &sig_, 1, 0);
    }
    errno = save_errno;
}

/* 注册信号 */
static void add_sig(int sig) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART; // 信号中断的系统调用会继续执行
    sigfillset(&sa.sa_mask); // 信号处理过程中屏蔽所有的信号
    assert(sigaction(sig, &sa, nullptr) != -1);
}

/* 设置fd为非阻塞 */
static void set_no_blocking(int cli_fd) {
    int old_op = fcntl(cli_fd, F_GETFL);
    int new_op = old_op | O_NONBLOCK;
    fcntl(cli_fd, F_SETFL, new_op);
}

/* 接受新的客户端连接 */
static int serv_accept(int listen_fd) {
    sockaddr_in cli_addr{}; // 用于保存client socket地址
    bzero(&cli_addr, sizeof(cli_addr));

    /* accept */
    socklen_t cli_addr_len = sizeof(cli_addr);
    int cli_fd = accept(listen_fd, (sockaddr*) &cli_addr, &cli_addr_len);
    if (cli_fd < 0) {
        std::cout << "Accept failed with errno: " << errno << ", errno str: " << strerror(errno) << std::endl;
        SYS_LOGW(MULT_CON_SRV_TAG, "Accept failed with errno: %d, errno str: %s.", errno, strerror(errno));
        return -1;
    }

    std::cout << "Connected with client, fd: " << cli_fd << ", client addr: " << inet_ntoa(cli_addr.sin_addr)
              << ", client port: " << ntohs(cli_addr.sin_port) << std::endl;
    SYS_LOGN(MULT_CON_SRV_TAG, "Connected with client, fd: %d, client addr: %s, client port: %d.", cli_fd,
             inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

    set_no_blocking(cli_fd);

    return cli_fd;
}

/* 读取客户端发来的信息 */
static int serv_read(int cli_fd) {
    char buf[BUF_SIZE];
    memset(buf, 0, BUF_SIZE);
    ssize_t n = recv(cli_fd, buf, BUF_SIZE - 1, 0);

    std::this_thread::get_id();
    std::thread::id thread_id = std::this_thread::get_id();
    unsigned long th_id = 0;
    memcpy(&th_id, &thread_id, sizeof(unsigned long));

    if (n > 0) { // 收到有效msg
        std::cout << "[Thread-" << th_id << "] Recv from fd(" << cli_fd << ") msg len: " << n
                  << ", msg: " << buf << std::endl;
        SYS_LOGI(MULT_CON_SRV_TAG, "[Thread-0x%lx] Recv msg from fd(%d), len: %zd, msg: %s", th_id, cli_fd, n, buf);
    } else if (n < 0) { // 收包出错
        std::cout << "[Thread-" << th_id << "] Recv from fd(" << cli_fd << ") failed with error num: " << errno
                  << ", error str: " << strerror(errno) << std::endl;
        SYS_LOGW(MULT_CON_SRV_TAG, "[Thread-0x%lx] Recv from fd(%d) failed with error num: %d, error str: %s.", th_id,
                 cli_fd, errno, strerror(errno));
    } else { // 对方断开了链接
        std::cout << "[Thread-" << th_id << "] Remote fd(" << cli_fd << ") disconnect" << std::endl;
        SYS_LOGN(MULT_CON_SRV_TAG, "[Thread-0x%lx] Remote fd(%d) disconnect.", th_id, cli_fd);
    }
    return n;
}

/********************************************* select多路复用处理多连接事件 *********************************************/
static void serv_select(int listen_fd) {
    int fds[SELECT_FDS_LEN];
    int maxfd = -1;
    memset(fds, -1, sizeof(fds)); // 初始化fd集合均为-1
    fds[0] = listen_fd; // fd[0]为监听连接的fd，即server fd

    fd_set rfds; // 监听可读事件的fd集合
    while (true) {
        /* 设置监听可读事件的fd集合，对所有有效fd监听 */
        FD_ZERO(&rfds);
        for (int i = 0; i < SELECT_FDS_LEN; i++) {
            if (fds[i] != -1) {
                FD_SET(fds[i], &rfds);
                maxfd = std::max(fds[i], maxfd);
            }
        }

        std::cout << "Selecting..." << std::endl;
        SYS_LOGN(MULT_CON_SRV_TAG, "Selecting...");

        struct timeval timeout = {10, 0}; // 超时时间设为10s
        switch (select(maxfd + 1, &rfds, nullptr, nullptr, &timeout)) {
        case 0:
            /* Select 超时 */
            std::cout << "Timeout." << std::endl;
            SYS_LOGN(MULT_CON_SRV_TAG, "Timeout.");
            break;
        case -1:
            /* Select 失败 */
            std::cout << "Select failed with errno: " << errno << ", errno str: " << strerror(errno) << std::endl;
            SYS_LOGE(MULT_CON_SRV_TAG, "Select failed with errno: %d, errno str: %s", errno, strerror(errno));
            exit(1);
            break;
        default:
            /* Select到有效事件 */
            std::cout << "Read event." << std::endl;
            SYS_LOGN(MULT_CON_SRV_TAG, "Read event.");
            if (FD_ISSET(listen_fd, &rfds)) {
                /* 如果监听连接fd有事件，则处理新的连接 */
                int cli_fd = serv_accept(listen_fd);
                if (-1 != cli_fd) {
                    int i = 0;
                    for (i = 0; i < SELECT_FDS_LEN; i++) {
                        if (fds[i] == -1) {
                            fds[i] = cli_fd;
                            std::cout << "Add new client fd into fd set." << std::endl;
                            SYS_LOGI(MULT_CON_SRV_TAG, "Add new client fd[%d] into fd set.", cli_fd);
                            break;
                        }
                    }
                    if (i == SELECT_FDS_LEN) {
                        close(cli_fd);
                        std::cout << "Fd set is full, disconnect client connection." << std::endl;
                        SYS_LOGW(MULT_CON_SRV_TAG, "Fd set is full, disconnect client connection.");
                    }
                }
            }
            /* 处理客户端fd发来的信息 */
            for (int i = 1; i < SELECT_FDS_LEN; i++) {
                if ((fds[i] != -1) && FD_ISSET(fds[i], &rfds)) {
                    int ret = serv_read(fds[i]);
                    if (ret == 0 || (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                        close(fds[i]);
                        fds[i] = -1;
                    }
                }
            }
        }
    }
}

/********************************************* poll多路复用处理多连接事件 *********************************************/
static void serv_poll(int listen_fd) {
    pollfd pfds[POLL_FDS_LEN];
    pfds[0].fd = listen_fd;
    pfds[0].events = POLLIN;
    for (int i = 1; i < POLL_FDS_LEN; i++) { // 初始化所有poll fd为-1
        pfds[i].fd = -1;
    }

    while (true) {
        std::cout << "Poll..." << std::endl;
        SYS_LOGN(MULT_CON_SRV_TAG, "Poll...");

        switch (poll(pfds, POLL_FDS_LEN, 10000)) {
        case 0:
            /* Poll 超时 */
            std::cout << "Timeout." << std::endl;
            SYS_LOGN(MULT_CON_SRV_TAG, "Timeout.");
            break;
        case -1:
            /* Poll 失败 */
            std::cout << "Poll failed with errno: " << errno << ", errno str: " << strerror(errno) << std::endl;
            SYS_LOGE(MULT_CON_SRV_TAG, "Poll failed with errno: %d, errno str: %s", errno, strerror(errno));
            exit(1);
        default:
            /* Poll到有效事件 */
            if (pfds[0].revents & POLLIN) {
                /* 如果监听连接fd有事件，则处理新的连接 */
                int cli_fd = serv_accept(listen_fd);
                if (-1 != cli_fd) {
                    int i = 0;
                    for (; i < POLL_FDS_LEN; i++) {
                        if (pfds[i].fd == -1) {
                            pfds[i].fd = cli_fd;
                            pfds[i].events = POLLIN;
                            std::cout << "Add new client fd into fd set." << std::endl;
                            SYS_LOGI(MULT_CON_SRV_TAG, "Add new client fd[%d] into fd set.", cli_fd);
                            break;
                        }
                    }
                    if (i == POLL_FDS_LEN) {
                        std::cout << "Fd set is full, disconnect client connection." << std::endl;
                        SYS_LOGW(MULT_CON_SRV_TAG, "Fd set is full, disconnect client connection.");
                    }
                }
            }
            /* 处理客户端fd发来的信息 */
            for (int i = 1; i < POLL_FDS_LEN; i++) {
                if (pfds[i].fd != -1 && pfds[i].revents & POLLIN) {
                    int ret = serv_read(pfds[i].fd);
                    if (ret == 0 || (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                        close(pfds[i].fd);
                        pfds[i].fd = -1;
                    }
                }
            }
        }
    }
}

/********************************************* epoll多路复用 + reactor处理多连接事件 *********************************************/
/*独立线程处理信号*/
static void ep_sig_worker_func(void) {
    std::thread::id thread_id = std::this_thread::get_id();
    unsigned long th_id = 0;
    memcpy(&th_id, &thread_id, sizeof(unsigned long));

    int8_t sig;
    while (recv(sig_pipe_fd[0], &sig, 1, 0) == 1) {
        std::cout << "[Thread-" << th_id << "] Handling signal: " << (int) sig << std::endl;
        SYS_LOGN(MULT_CON_SRV_TAG, "[Thread-0x%lx] Handling signal: %d", th_id, sig);
        switch (sig) {
        case SIGTERM:
        case SIGINT:
            running = false;
        default:
            continue;
        }
    }
}

/* 独立线程处理客户端的连接请求 */
static void ep_accept_worker_func(int listen_fd, EpollTriggerType trigger_type) {
    std::thread::id thread_id = std::this_thread::get_id();
    unsigned long th_id = 0;
    memcpy(&th_id, &thread_id, sizeof(unsigned long));

    int cli_fd = serv_accept(listen_fd);
    if (-1 != cli_fd) {
        std::lock_guard<std::mutex> epfd_lock(epfd_mutex); // 需要修改epfd，ep_fds值，使用互斥元保护
        if (ep_fds.size() < EPOLL_FDS_LEN) {
            epoll_event event{};
            event.data.fd = cli_fd;
            event.events = EPOLLIN;
            if (trigger_type == EP_ET) {
                event.events |= EPOLLET; // 设置edge trigger
            }

            epoll_ctl(epfd, EPOLL_CTL_ADD, cli_fd, &event);
            ep_fds.insert(cli_fd);

            std::cout << "[Thread-" << th_id << "] Add new client fd(" << cli_fd << ") into fd set, fds_len: "
                      << ep_fds.size() << std::endl;
            SYS_LOGI(MULT_CON_SRV_TAG, "[Thread-0x%lx] Add new client fd(%d) into fd set, fds_len: %lu.", th_id, cli_fd,
                     ep_fds.size());
        } else {
            close(cli_fd);
            std::cout << "[Thread-" << th_id << "] Fd set is full, disconnect client connection." << std::endl;
            SYS_LOGW(MULT_CON_SRV_TAG, "[Thread-0x%lx] Fd set is full, disconnect client connection.", th_id);
        }
    }
}

/* 独立线程处理客户端发来的信息 */
static void ep_msg_worker_func(int cli_fd, EpollTriggerType trigger_type) {
    std::thread::id thread_id = std::this_thread::get_id();
    unsigned long th_id = 0;
    memcpy(&th_id, &thread_id, sizeof(unsigned long));
    if (trigger_type == EP_ET) {
        /* 当前是edge trigger，一定保证将当前读缓存读完，因为该触发模式下会丢掉当前事件，即使没处理或没处理完 */
        while (true) {
            int ret = serv_read(cli_fd);
            if (ret > 0) {
                continue; // 继续读完所有读缓存
            } else if (ret == 0) {
                // 远端关闭了连接
                close(cli_fd);
                goto CLOSE_FD;
                break;
            } else if (ret < 0) {
                if ((errno == EAGAIN || errno == EWOULDBLOCK)) {
                    /* 等待下一个EPOLL_IN事件再读 */
                    std::cout << "[Thread-" << th_id << "] Edge trigger, wait next EPOLL_IN." << std::endl;
                    SYS_LOGW(MULT_CON_SRV_TAG, "[Thread-0x%lx] Edge trigger, wait next EPOLL_IN.", th_id);
                } else {
                    /* recv异常，断开连接 */
                    close(cli_fd);
                    goto CLOSE_FD;
                }
                break;
            }
        }
    } else {
        /* 当前是level trigger，不需要保证将当前读缓存读完，因为该触发模式如果没处理或没处理完该事件，下次保留当前事件 */
        int ret = serv_read(cli_fd);
        if (ret == 0) {
            close(cli_fd);
            goto CLOSE_FD;
        } else if (ret < 0) {
            if ((errno == EAGAIN || errno == EWOULDBLOCK)) {
                /* 等待下一个EPOLL_IN事件再读 */
                std::cout << "[Thread-" << th_id << "] Level trigger, wait next EPOLL_IN." << std::endl;
                SYS_LOGW(MULT_CON_SRV_TAG, "[Thread-0x%lx] Level trigger, wait next EPOLL_IN.", th_id);
            } else {
                /* recv异常，断开连接 */
                close(cli_fd);
                goto CLOSE_FD;
            }
        }
    }
    return;

    CLOSE_FD:
    std::unique_lock<std::mutex> epfd_lock(epfd_mutex); // 需要修改epfd，fds_len值，使用互斥元保护
    epoll_ctl(epfd, EPOLL_CTL_DEL, cli_fd, nullptr);
    ep_fds.erase(cli_fd);
    epfd_lock.unlock();
    std::cout << "[Thread-" << th_id << "] Close fd(" << cli_fd << "), fds_len: " << ep_fds.size() << std::endl;
    SYS_LOGW(MULT_CON_SRV_TAG, "[Thread-0x%lx] Close fd(%d), fds_len:%lu.", th_id, cli_fd, ep_fds.size());
}

static void serv_epoll(int listen_fd, EpollTriggerType trigger_type) {
    epfd = epoll_create(EPOLL_FDS_LEN);

    /* 添加监听客户端连接事件以及信号处理事件 */
    epoll_event event{};
    event.data.fd = listen_fd;
    event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &event);
    event.data.fd = sig_pipe_fd[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, sig_pipe_fd[0], &event);
    ep_fds.insert(listen_fd);
    ep_fds.insert(sig_pipe_fd[0]);

    unsigned int concur_count = std::thread::hardware_concurrency();
    boost::asio::thread_pool pool(concur_count); // 根据当前系统硬件并发数量创建线程池
    std::cout << "Create thread pool with concurrent count: " << concur_count << std::endl;
    SYS_LOGN(MULT_CON_SRV_TAG, "Create thread pool with concurrent count: %d.", concur_count);

    while (running) {
        std::cout << "Epoll..." << std::endl;
        SYS_LOGN(MULT_CON_SRV_TAG, "Epoll...");

        epoll_event events[EPOLL_FDS_LEN];

        std::unique_lock<std::mutex> epfd_lock(epfd_mutex); // 需要读取epfd，使用互斥元保护
        int num_events = epoll_wait(epfd, events, EPOLL_FDS_LEN, 5000);
        epfd_lock.unlock();

        switch (num_events) {
        case 0:
            /* Epoll 超时 */
            std::cout << "Timeout." << std::endl;
            SYS_LOGN(MULT_CON_SRV_TAG, "Timeout.");
            break;
        case -1:
            /* Epoll 失败 */
            std::cout << "Epoll failed with errno: " << errno << ", errno str: " << strerror(errno) << std::endl;
            SYS_LOGE(MULT_CON_SRV_TAG, "Epoll failed with errno: %d, errno str: %s", errno, strerror(errno));
            exit(1);
        default:
            // std::cout << "Epoll received event num: " << num_events << std::endl;
            // SYS_LOGI(MULT_CON_SRV_TAG, "Epoll received event num: %d.", num_events);
            for (int i = 0; i < num_events; i++) {
                if ((int) events[i].data.fd == listen_fd && events[i].events & EPOLLIN) {
                    /* 将接受客户端连接的任务放在主线程中做，使得fd及时加入到监听中 */
                    ep_accept_worker_func(listen_fd, trigger_type);
                } else if ((int) events[i].data.fd == sig_pipe_fd[0] && events[i].events & EPOLLIN) {
                    /* 将信号处理任务分派到线程池中的某个线程 */
                    boost::asio::dispatch(pool, ep_sig_worker_func);
                } else if ((int) events[i].data.fd != -1 && events[i].events & EPOLLIN) {
                    /* 将读取客户端信息的任务分派给线程池中的某个线程 */
                    int fd = events[i].data.fd;
                    boost::asio::dispatch(pool, [fd, trigger_type]() {
                        ep_msg_worker_func(fd, trigger_type);
                    });
                }
            }
        }
    }

    std::cout << "Multi connection server stopped." << std::endl;
    SYS_LOGN(MULT_CON_SRV_TAG, "Multi connection server stopped.");
    for (auto& fd: ep_fds) { // 关闭epoll监听的所有fd
        close(fd);
    }
    close(sig_pipe_fd[1]);
    exit(0);
}

/**
* 开启多客户端服务器
* @param ip: ip地址
* @param port: 端口
* @param multiplex_type: 多路复用机制类型
* @param trigger_type: 如果是epoll机制，则需要指定trigger类型，其他机制忽略
*/
void start_multi_con_server(const char* ip, int port, MultiplexType multiplex_type,
                            EpollTriggerType trigger_type = EP_ET) {
    /* 注册信号，创建信号传递通道 */
    socketpair(AF_UNIX, SOCK_STREAM, 0, sig_pipe_fd);
    set_no_blocking(sig_pipe_fd[0]);
    set_no_blocking(sig_pipe_fd[1]);
    add_sig(SIGTERM);
    add_sig(SIGINT); // ctrl + c
    add_sig(SIGPIPE); // 写通道关闭触发
    add_sig(SIGURG); // 收到带外数据触发
    add_sig(SIGALRM); // 系统定时器

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
    SYS_LOGI(MULT_CON_SRV_TAG, "Server begin listening...");

    /* 多路复用 */
    switch (multiplex_type) {
    case MT_SELECT:
        serv_select(srv_fd);
        break;
    case MT_POLL:
        serv_poll(srv_fd);
        break;
    case MT_EPOLL:
    default:
        serv_epoll(srv_fd, trigger_type);
        break;
    }
}

#endif //SERVER_MULTI_CONNECTION_SERVER_HPP
