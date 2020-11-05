/**
* 一些工具方法
*/

#ifndef SERVER_TOOLS_HPP
#define SERVER_TOOLS_HPP

#include <iostream>
#include <unistd.h>
#include <cassert>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/syslog.h>

/* 系统日志宏 */
#define SYS_LOGE(tag, format, ...) syslog(LOG_ERR, "[" tag "]" "  ERROR  " format, ##__VA_ARGS__)
#define SYS_LOGW(tag, format, ...) syslog(LOG_WARNING, "[" tag "]" "  WARNING  " format, ##__VA_ARGS__)
#define SYS_LOGN(tag, format, ...) syslog(LOG_NOTICE, "[" tag "]" "  NOTICE  " format, ##__VA_ARGS__)
#define SYS_LOGI(tag, format, ...) syslog(LOG_INFO, "[" tag "]" "  INFO  " format, ##__VA_ARGS__)
#define SYS_LOGD(tag, format, ...) syslog(LOG_DEBUG, "[" tag "]" "  DEBUG  " format, ##__VA_ARGS__)

/**
* 将进程置成守护进程
* @param no_change: 如果为true则代表不切换工作目录到根目录
* @param no_close: 如果为true则代表不关闭标准输入输出流
* @return
*/
bool daemonize(bool no_change, bool no_close) {
    std::cout << "Before daemonize pid: " << getpid() << ", ppid: " << getppid() << ", pgid: " << getpgid(getpid())
              << ", sid: " << getsid(getpid()) << "." << std::endl;
    std::cout << "Before daemonize uid: " << getuid() << ", gid: " << getgid() << ", euid: " << geteuid() << ", egid: "
              <<
              getegid() << "." << std::endl;

    /* 创建子进程，并且关闭父进程，此时子进程变成孤儿进程，由init进程托管 */
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    } else if (pid > 0) {
        // 父进程退出
        exit(0);
    }

    /* 设置文件权限掩码，open指定的mode - mask为真正的权限 */
    umask(0);

    /* 创建新的会话，成为会话唯一成员，成为会话组首领；创建进程组，成为进程组首领；甩开终端 */
    pid_t sid = setsid();
    if (sid < 0) {
        return false;
    }

    /* 切换工作路径到根目录 */
    if (!no_change && chdir("/") < 0) {
        return false;
    }

    std::cout << "After daemonize pid: " << getpid() << ", ppid: " << getppid() << ", pgid: " << getpgid(getpid())
              << ", sid: " << getsid(getpid()) << "." << std::endl;
    std::cout << "After daemonize uid: " << getuid() << ", gid: " << getgid() << ", euid: " << geteuid() << ", egid: "
              << getegid() << "." << std::endl;

    if (no_close) {
        return true;
    }

    /* 关闭标准输入设备，标准输出设备，和标准错误输出设备 */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    /* 将标准输入，输出，错误输出重定向到/dev/null */
    assert(open("/dev/null", O_RDONLY) == 0);
    assert(open("/dev/null", O_RDWR) == 1);
    assert(open("/dev/null", O_RDWR) == 2);

    return true;
}

#endif //SERVER_TOOLS_HPP
