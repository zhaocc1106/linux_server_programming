/**
* 使用epoll多路复用 + half-sync/half-reactive事件处理模式 + 进程池实现及时处理多请求的server
*/

#ifndef SERVER_EPOLL_HALF_SYNC_REACTIVE_PROC_POOL_SERVER_HPP
#define SERVER_EPOLL_HALF_SYNC_REACTIVE_PROC_POOL_SERVER_HPP

#include <iostream>
#include <unordered_map>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "tools.hpp"

#define PROC_POOL_SERVER "ProcessPoolServer"

/*用于处理信号的管道，以实现统一事件源。后面称之为信号管道*/
static int sig_pipefd[2];

/**
* 将fd设置为非阻塞
* @param fd
* @return 修改之前的file option
*/
static int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/**
* 在epollfd标识的epoll时间表中添加监听的fd
* @param epollfd: epoll fd
* @param fd: 被监听的fd
*/
static void addfd(int epollfd, int fd) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/**
* 从epollfd标识的epoll内核事件表中删除fd上的所有注册事件
* @param epollfd
* @param fd
*/
static void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/**
* 信号处理函数
* @param sig: 信号
*/
static void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char*) &msg, 1, 0); // 发送信号到pipe中，通过epoll监听该fd实现统一处理信号和io事件
    errno = save_errno;
}

/**
* 注册信号处理
* @param sig: 信号
* @param handler: 信号处理函数
* @param restart: 信号处理完后是否重启打断的系统调用
*/
static void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa{};
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

/*描述一个子进程的类，m_pid是目标子进程的PID，m_pipefd是父进程和子进程通信用的管道*/
class Process {
public:
    Process() : m_pid(-1) {}

public:
    pid_t m_pid;
    int m_pipefd[2]{};
};

/*服务器进程池类，将它定义为模板类是为了代码复用。其模板参数是处理逻辑任务的类*/
template<typename T>
class SrvProcessPool {
public:
    /*单体模式，以保证程序最多创建一个SrvProcessPool实例，这是程序正确处理信号的必要条件*/
    static SrvProcessPool<T>& create(int listenfd, int process_number = 8) {
        static SrvProcessPool<T> srv_process_pool(listenfd, process_number);
        return srv_process_pool;
    }

    ~SrvProcessPool() {
        delete[]_sub_process;
    }

    /**
    * 启动进程池
    */
    void run();

private:
    /**
    * 进程池构造函数，将构造函数定义为私有的，因此我们只能通过后面的create静态函数来创建SrvProcessPool实例
    * @param listenfd: 监听socket，它必须在创建进程池之前被创建，否则子进程无法直接引用它。
    * @param process_number: 指定进程池中子进程的数量
    */
    explicit SrvProcessPool(int listenfd, int process_number = 8);

    /**
    * 注册信号事件
    */
    void setup_sig_pipe();

    /**
    * 父进程执行的工作函数
    */
    void run_parent();

    /**
    * 子进程执行的工作函数
    */
    void run_child();

    /*进程池允许的最大子进程数量*/
    static const int MAX_PROCESS_NUMBER = 16;
    /*每个子进程最多能处理的客户数量*/
    static const int USER_PER_PROCESS = 65536;
    /*epoll最多能处理的事件数*/
    static const int MAX_EVENT_NUMBER = 10000;
    /*进程池中的进程总数*/
    int _process_number;
    /*子进程在池中的序号，从0开始*/
    int _idx;
    /*每个进程都有一个epoll内核事件表，用_epollfd标识*/
    int _epollfd;
    /*监听socket*/
    int _listenfd;
    /*子进程通过_stop来决定是否停止运行*/
    int _stop;
    /*保存所有子进程的描述信息*/
    Process* _sub_process;
};

/**
* 进程池构造函数。
* @param listenfd: 监听socket，它必须在创建进程池之前被创建，否则子进程无法直接引用它。
* @param process_number: 指定进程池中子进程的数量
*/
template<typename T>
SrvProcessPool<T>::SrvProcessPool(int listenfd, int process_number)
        :_listenfd(listenfd), _process_number(process_number), _idx(-1),
         _stop(false) {
}

/**
* 打开信号处理pipe，统一处理信号事件
*/
template<typename T>
void SrvProcessPool<T>::setup_sig_pipe() {
    /*创建epoll事件监听表和信号管道*/
    _epollfd = epoll_create(5);
    assert(_epollfd != -1);
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);
    setnonblocking(sig_pipefd[1]);
    addfd(_epollfd, sig_pipefd[0]);
    /*设置信号处理函数*/
    addsig(SIGCHLD, sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT, sig_handler);
    addsig(SIGPIPE, SIG_IGN);
}

/**
* 创建子进程，并且创建子进程与主进程通信的通道，最后根据当前进程启动不同逻辑
*/
template<typename T>
void SrvProcessPool<T>::run() {
    assert((_process_number > 0) && (_process_number <= MAX_PROCESS_NUMBER));
    _sub_process = new Process[_process_number];
    assert(_sub_process);
    /* 创建_process_number个子进程，并建立它们和父进程之间的管道 */
    for (int i = 0; i < _process_number; ++i) {
        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, _sub_process[i].m_pipefd);
        assert(ret == 0);
        _sub_process[i].m_pid = fork();
        assert(_sub_process[i].m_pid >= 0);
        if (_sub_process[i].m_pid > 0) {
            // 主进程
            close(_sub_process[i].m_pipefd[1]);
            setnonblocking(_sub_process[i].m_pipefd[0]);
            continue;
        } else {
            // 子进程
            close(_sub_process[i].m_pipefd[0]);
            setnonblocking(_sub_process[i].m_pipefd[1]);
            _idx = i;
            break;
        }
    }

    if (_idx != -1) {
        run_child();
        return;
    }
    run_parent();
}

/**
* 子进程执行的工作函数
*/
template<typename T>
void SrvProcessPool<T>::run_child() {
    std::cout << "[Process-" << _sub_process[_idx].m_pid << "] Begin running..." << std::endl;
    SYS_LOGN(PROC_POOL_SERVER, "[Process-0x%x] Begin running...", _sub_process[_idx].m_pid);

    setup_sig_pipe();
    /* 每个子进程都通过其在进程池中的序号值_idx找到与父进程通信的管道 */
    int pipefd = _sub_process[_idx].m_pipefd[1];
    /* 子进程需要监听管道文件描述符pipefd，因为父进程将通过它来通知子进程accept新连接 */
    addfd(_epollfd, pipefd);

    epoll_event events[MAX_EVENT_NUMBER];
    T* users = new T[USER_PER_PROCESS]; // 保存所有的client请求，使用fd作为index来保存所有客户端请求处理对象
    int ret = -1;
    while (!_stop) {
        int number = epoll_wait(_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) {
            std::cout << "[Process-" << _sub_process[_idx].m_pid << "] epoll failed errno string: " << strerror(errno)
                      << std::endl;
            SYS_LOGW(PROC_POOL_SERVER, "[Process-0x%x] epoll failed errno string: %s", _sub_process[_idx].m_pid,
                     strerror(errno));
            break;
        }
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            if ((sockfd == pipefd) && (events[i].events & EPOLLIN)) {
                /* 从父、子进程之间的管道读取数据，并将结果保存在变量client中。如果读取成功，则表示有新客户连接到来 */
                int new_con = 0;
                ret = recv(sockfd, (char*) &new_con, sizeof(new_con), 0);
                if (ret <= 0) {
                    continue;
                } else {
                    std::cout << "[Process-" << _sub_process[_idx].m_pid
                              << "] Received connection distribution from main process." << std::endl;
                    SYS_LOGN(PROC_POOL_SERVER, "[Process-0x%x] Received connection distribution from main process.",
                             _sub_process[_idx].m_pid);

                    struct sockaddr_in client_address{};
                    socklen_t client_addrlength = sizeof(client_address);
                    int connfd = accept(_listenfd, (struct sockaddr*) &client_address, &client_addrlength);
                    if (connfd < 0) {
                        std::cout << "[Process-" << _sub_process[_idx].m_pid << "] accept failed, errno string: "
                                  << strerror(errno);
                        SYS_LOGW(PROC_POOL_SERVER, "[Process-0x%x] accept failed, errno string: %s.",
                                 _sub_process[_idx].m_pid, strerror(errno));
                        break;
                    }
                    addfd(_epollfd, connfd);
                    /* 模板类T必须实现init方法，以初始化一个客户连接。 */
                    users[connfd].init(_epollfd, connfd, client_address);
                }
            } else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)) {
                /* 下面处理子进程接收到的信号 */
                while (true) {
                    int sig;
                    char signals[1024];
                    ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                    if (ret <= 0) {
                        break;
                    } else {
                        for (auto& signal : signals) {
                            switch (signal) {
                            case SIGCHLD: {
                                pid_t pid;
                                int stat;
                                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0);
                                break;
                            }
                            case SIGTERM:
                            case SIGINT: {
                                _stop = true;
                                break;
                            }
                            default: {
                                break;
                            }
                            }
                        }
                    }
                }
            } else if (events[i].events & EPOLLIN) {
                /*如果是其他可读数据，那么必然是客户请求到来。调用逻辑处理对象的process方法处理之*/
                users[sockfd].process();
            } else {
                continue;
            }
        }
    }

    delete[] users;
    users = nullptr;
    close(pipefd);
    //close(_listenfd);
    /*我们将这句话注释掉，以提醒读者：应该由_listenfd的创建者来关闭这个文件描述符（见后文），即所谓的“对象（比如一个文件描述符，又或者一段堆内存）由哪个函数创建，就应该由哪个函数销毁” */
    close(_epollfd);
}

/**
* 父进程执行的工作函数
*/
template<typename T>
void SrvProcessPool<T>::run_parent() {
    setup_sig_pipe();
    /*父进程监听_listenfd*/
    addfd(_epollfd, _listenfd);

    epoll_event events[MAX_EVENT_NUMBER];
    int sub_process_counter = 0;
    int new_conn = 1;
    int ret = -1;
    while (!_stop) {
        int number = epoll_wait(_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) {
            std::cout << "[Main process] epoll failed errno string: " << strerror(errno) << std::endl;
            SYS_LOGW(PROC_POOL_SERVER, "[Main process] epoll failed errno string: %s", strerror(errno));
            break;
        }
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == _listenfd) {
                /* 如果有新连接到来，就采用Round Robin方式将其分配给一个子进程处理 */
                int j = sub_process_counter;
                do {
                    if (_sub_process[j].m_pid != -1) {
                        break;
                    }
                    j = (j + 1) % _process_number;
                } while (j != sub_process_counter);
                if (_sub_process[j].m_pid == -1) {
                    _stop = true;
                    break;
                }
                sub_process_counter = (j + 1) % _process_number;
                send(_sub_process[j].m_pipefd[0], (char*) &new_conn, sizeof(new_conn), 0);
            } else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)) {
                /* 下面处理父进程接收到的信号 */
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if (ret <= 0) {
                    continue;
                } else {
                    for (char& signal : signals) {
                        switch (signal) {
                        case SIGCHLD: {
                            pid_t pid;
                            int stat;
                            while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                                for (int j = 0; j < _process_number; ++j) {
                                    /* 如果进程池中第i个子进程退出了，则主进程关闭相应的通信管道，并设置相应的m_pid为-1，以标记该子进程已经退出 */
                                    if (_sub_process[j].m_pid == pid) {
                                        std::cout << "[Main process] Sub process[" << pid << "] exit." << std::endl;
                                        SYS_LOGW(PROC_POOL_SERVER, "[Main process] Sub process[0x%x] exit.", pid);
                                        close(_sub_process[j].m_pipefd[0]);
                                        _sub_process[j].m_pid = -1;
                                    }
                                }
                            }
                            /*如果所有子进程都已经退出了，则父进程也退出*/
                            _stop = true;
                            for (int i = 0; i < _process_number; ++i) {
                                if (_sub_process[i].m_pid != -1) {
                                    _stop = false;
                                }
                            }
                            break;
                        }
                        case SIGTERM:
                        case SIGINT: {
                            /*如果父进程接收到终止信号，那么就杀死所有子进程，并等待它们全部结束。当然，通知子进程结束更好的方法是向父、子进程之间的通信管道发送特殊数据，读者不妨自己实现之*/
                            std::cout << "[Main process] Received termination or interrupt signal kill all the clild "
                                         "now." << std::endl;
                            SYS_LOGW(PROC_POOL_SERVER, "[Main process] Received termination or interrupt signal kill "
                                                       "all the clild now.");
                            for (int i = 0; i < _process_number; ++i) {
                                int pid = _sub_process[i].m_pid;
                                if (pid != -1) {
                                    kill(pid, SIGTERM);
                                }
                            }
                            break;
                        }
                        default: {
                            break;
                        }
                        }
                    }
                }
            } else {
                continue;
            }
        }
    }
    //close(_listenfd);
    /*由创建者关闭这个文件描述符*/
    close(_epollfd);
}

/* 用于处理客户请求的类，可以作为SrvProcessPool的模板参数 */
class TestConn {
public:
    TestConn() : _epollfd(-1), _cli_fd(-1) {}

    ~TestConn() = default;

    /**
    * 初始化
    * @param epollfd: epoll fd
    * @param cli_fd: 客户端fd
    * @param cli_addr: 客户端地址
    */
    void init(int epollfd, int cli_fd, struct sockaddr_in cli_addr);

    /**
    * 处理客户端请求
    */
    void process() const;

private:
    int _epollfd; // 进程中监听客户端fd的epollfd
    int _cli_fd; // 客户端fd
    struct sockaddr_in _cli_addr{}; // 客户端地址
};

/**
* 初始化
* @param epollfd: epoll fd
* @param cli_fd: 客户端fd
* @param cli_addr: 客户端地址
*/
void TestConn::init(int epollfd, int cli_fd, struct sockaddr_in cli_addr) {
    _epollfd = epollfd;
    _cli_fd = cli_fd;
    _cli_addr = cli_addr;
    setnonblocking(_cli_fd); // 设置客户端fd非阻塞
}

/**
* 处理客户端请求
*/
void TestConn::process() const {
    pid_t pid = getpid();

    /* 当前是edge trigger，一定保证将当前读缓存读完，因为该触发模式下会丢掉当前事件，即使没处理或没处理完 */
    while (true) {
        char buf[BUF_SIZE];
        memset(buf, 0, BUF_SIZE);
        ssize_t len = recv(_cli_fd, buf, BUF_SIZE - 1, 0);

        if (len > 0) {
            std::cout << "[Process-" << pid << "] Recv from fd(" << _cli_fd << ") msg len: " << len
                      << ", msg: " << buf << std::endl;
            SYS_LOGI(PROC_POOL_SERVER, "[Process-0x%x] Recv msg from fd(%d), len: %zd, msg: %s", pid, _cli_fd, len,
                     buf);
            continue; // 继续读完所有读缓存
        } else if (len == 0) {
            // 远端关闭了连接
            goto CLOSE_FD;
            break;
        } else if (len < 0) {
            if ((errno == EAGAIN || errno == EWOULDBLOCK)) {
                /* 等待下一个EPOLL_IN事件再读 */
                std::cout << "[Process-" << pid << "] Edge trigger, wait next EPOLL_IN." << std::endl;
                SYS_LOGW(PROC_POOL_SERVER, "[Thread-0x%x] Edge trigger, wait next EPOLL_IN.", pid);
            } else {
                /* recv异常，断开连接 */
                goto CLOSE_FD;
            }
            break;
        }
    }
    return;

    CLOSE_FD:
    removefd(_epollfd, _cli_fd);
    std::cout << "[Process-" << pid << "] Close fd(" << _cli_fd << ")." << std::endl;
    SYS_LOGW(PROC_POOL_SERVER, "[Process-0x%x] Close fd(%d).", pid, _cli_fd);
}

/**
* 开启server
* @param ip: ip地址
* @param port: 端口
* @param concur: 进程池并发数量
*/
void start_libev_adv_server(const char* ip, int port, int concur) {
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

    SrvProcessPool<TestConn>& srv_process_pool = SrvProcessPool<TestConn>::create(srv_fd, 8);
    srv_process_pool.run();
}

#endif //SERVER_EPOLL_HALF_SYNC_REACTIVE_PROC_POOL_SERVER_HPP
