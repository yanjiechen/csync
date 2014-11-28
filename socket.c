/*
 * Copyright (c) 2004 SOHU INC.
 * Author: Shao Hui <richardshao@sohu-inc.com>
 * Copyright (c) 2008 SOHU INC.
 * Modified by Qin Jianhua <jianhuaqin@sohu-inc.com>
 *
 * This file contains network functions.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <syslog.h>
#include <sys/poll.h>
#include <pthread.h>

#include "socket.h"
#include "log.h"
#include "conf.h"

#ifdef _DEBUG

static int on=1;

#define DEF_VALS {NULL, NULL, NULL}
#define DEF_ACCEPT {(void *)&on, NULL, NULL}

SOCK_OPT sock_opts[] = {
    {"SO_DEBUG",        SOL_SOCKET,  SO_DEBUG,        TYPE_FLAG,    DEF_VALS},
    {"SO_DONTROUTE",    SOL_SOCKET,  SO_DONTROUTE,    TYPE_FLAG,    DEF_VALS},
    {"SO_KEEPALIVE",    SOL_SOCKET,  SO_KEEPALIVE,    TYPE_FLAG,    DEF_VALS},
    {"SO_LINGER",       SOL_SOCKET,  SO_LINGER,       TYPE_LINGER,  DEF_VALS},
    {"SO_OOBINLINE",    SOL_SOCKET,  SO_OOBINLINE,    TYPE_FLAG,    DEF_VALS},
    {"SO_RCVBUF",       SOL_SOCKET,  SO_RCVBUF,       TYPE_INT,     DEF_VALS},
    {"SO_SNDBUF",       SOL_SOCKET,  SO_SNDBUF,       TYPE_INT,     DEF_VALS},
    {"SO_RCVLOWAT",     SOL_SOCKET,  SO_RCVLOWAT,     TYPE_INT,     DEF_VALS},
    {"SO_SNDLOWAT",     SOL_SOCKET,  SO_SNDLOWAT,     TYPE_INT,     DEF_VALS},
    {"SO_RCVTIMEO",     SOL_SOCKET,  SO_RCVTIMEO,     TYPE_TIMEVAL, DEF_VALS},
    {"SO_SNDTIMEO",     SOL_SOCKET,  SO_SNDTIMEO,     TYPE_TIMEVAL, DEF_VALS},
    {"SO_REUSEADDR",    SOL_SOCKET,  SO_REUSEADDR,    TYPE_FLAG,    DEF_ACCEPT},
    {"SO_BINDTODEVICE", SOL_SOCKET,  SO_BINDTODEVICE, TYPE_STRING,  DEF_VALS},
    {"IP_TOS",          IPPROTO_IP,  IP_TOS,          TYPE_INT,     DEF_VALS},
    {"IP_TTL",          IPPROTO_IP,  IP_TTL,          TYPE_INT,     DEF_VALS},
    {"TCP_MAXSEG",      IPPROTO_TCP, TCP_MAXSEG,      TYPE_INT,     DEF_VALS},
    {"TCP_NODELAY",     IPPROTO_TCP, TCP_NODELAY,     TYPE_FLAG,    DEF_VALS},
    {NULL,              0,           0,               TYPE_NONE,    DEF_VALS}
};

/*
 * Print socket options, for debug use.
 */
void print_socket_options(void) {
    int fd, len;
    SOCK_OPT *ptr;
    OPT_UNION val;
    char line[MAX_LINE_LEN];

    fd=socket(AF_INET, SOCK_STREAM, 0);

    log_msg(LOG_INFO, "Socket option defaults:");
    log_msg(LOG_INFO, "%-16s%-10s%-10s%-10s%-10s",
        "Option", "Accept", "Local", "Remote", "OS default");
    for(ptr=sock_opts; ptr->opt_str; ++ptr) {
        /* display option name */
        sprintf(line, "%-16s", ptr->opt_str);
        /* display stunnel default values */
        print_option(line, ptr->opt_type, ptr->opt_val[0]);
        print_option(line, ptr->opt_type, ptr->opt_val[1]);
        print_option(line, ptr->opt_type, ptr->opt_val[2]);
        /* display OS default value */
        len = sizeof(val);
        if(getsockopt(fd, ptr->opt_level, ptr->opt_name, (void *)&val, &len)) {
            if(errno!=ENOPROTOOPT) {
                log_msg(LOG_ERR, "%s", line);
                perror("getsockopt");
                return;
            }
            strcat(line, "    --    "); /* write-only value */
        } else
            print_option(line, ptr->opt_type, &val);
        log_msg(LOG_INFO, "%s", line);
    }
    return;
}

/*
 * Print one option.
 */
static void print_option(char *line, int type, OPT_UNION *val) {
    char text[MAX_LINE_LEN];

    if(!val) {
        strcpy(text, "    --    ");
    } else {
        switch(type) {
        case TYPE_FLAG:
        case TYPE_INT:
            sprintf(text, "%10d", val->i_val);
            break;
        case TYPE_LINGER:
            sprintf(text, "%d:%-8d",
                val->linger_val.l_onoff, val->linger_val.l_linger);
            break;
        case TYPE_TIMEVAL:
            sprintf(text, "%6d:%-3d",
                (int)val->timeval_val.tv_sec, (int)val->timeval_val.tv_usec);
            break;
        case TYPE_STRING:
            sprintf(text, "%10s", val->c_val);
            break;
        default:
            strcpy(text, "  Ooops?  "); /* Internal error? */
        }
    }
    strcat(line, text);
}
#endif

/*
 * Get the address of a domain name.
 */
int get_addr(char *addr, struct in_addr *saddr)
{
    struct hostent *hp;

    hp = gethostbyname(addr);
    if (hp == NULL) {
        return -1;
    } else {
        memcpy(saddr, *(hp->h_addr_list), sizeof(struct in_addr));
        return 0;
    }
}

/*
 * Get an server socket, listen on addr:port.
 */
int server_socket(char *addr, char *bind_port)
{
    int sock;
    int sockval = 1;
    struct addrinfo hints, *res;
    int bind_error;

    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        log_msg(LOG_ERR, "socket: can't create socket");
        return -1;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&sockval,
            sizeof(sockval))) {
        log_msg(LOG_ERR, "socket: can't setsockopt: SO_REUSEADDR");
        close(sock);
        return -1;
    }
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void *)&sockval,
            sizeof(sockval))) {
        log_msg(LOG_ERR, "socket: can't setsockopt: TCP_NODELAY");
        close(sock);
        return -1;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    bind_error = getaddrinfo(addr, bind_port, &hints, &res);
    if (bind_error != 0) {
        log_msg(LOG_ERR, "socket: can't getaddrinfo for %s: %s", addr, gai_strerror(bind_error));
        close(sock);
        return -1;
    }
    if (bind(sock, res->ai_addr, res->ai_addrlen) == -1) {
        log_msg(LOG_ERR, "socket: can't bind socket");
        freeaddrinfo(res);
        close(sock);
        return -1;
    }
    if (listen(sock, 128) == -1) {
        log_msg(LOG_ERR, "socket: can't listen socket");
        freeaddrinfo(res);
        close(sock);
        return -1;
    }
    freeaddrinfo(res);
    return sock;
}

/*
 * Get and client socket, connected to add:port.
 */
int client_socket(char *addr, char *connect_port, pthread_t connect_tid)
{
    int sock;
    int sockval = 1;
    struct addrinfo hints, *res;
    int connect_error;

    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        log_msg(LOG_ERR, "socket: can't create socket");
        return -1;
    }
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void *)&sockval,
            sizeof(sockval))) {
        log_msg(LOG_ERR, "socket: can't setsockopt: TCP_NODELAY");
        close(sock);
        return -1;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    connect_error = getaddrinfo(addr, connect_port, &hints, &res);
    if (connect_error != 0) {
        log_msg(LOG_ERR, "sync client(%u) socket: can't getaddrinfo for %s: %s",
                connect_tid, addr, gai_strerror(connect_error));
        close(sock);
        return -1;
    }
    if (connect(sock, res->ai_addr, res->ai_addrlen) == -1) {
        log_msg(LOG_ERR, "sync client(%u) socket: can't connect to host: %s(%s):%s",
                connect_tid, addr, inet_ntoa(((struct sockaddr_in *)res->ai_addr)->sin_addr), connect_port);
        freeaddrinfo(res);
        close(sock);
        return -1;
    }
    log_msg(LOG_INFO, "sync client(%u): connected to server %s(%s):%s",
            connect_tid, addr, inet_ntoa(((struct sockaddr_in *)res->ai_addr)->sin_addr), connect_port);
        
    freeaddrinfo(res);
    return sock;
}

/*
 * Read n bytes from a socket.
 */
ssize_t readn(int fd, void *vptr, size_t n)
{
    ssize_t nleft, nread;
    char *ptr;
    struct pollfd ufd;
    int nfd;

    ptr = vptr;
    nleft = n;
    ufd.fd = fd;
    ufd.events = POLLIN;
    ufd.revents = 0;
    while (nleft > 0) {
        nfd = poll(&ufd, 1, 100);
        if (nfd == 1) {
            if (ufd.revents & POLLIN) {
                if ((nread = read(fd, ptr, nleft)) < 0) {
                    if (errno == EINTR)
                        nread = 0;
                    else
                        return -1;
                } else if (nread == 0)
                    break;
                nleft -= nread;
                ptr += nread;
            } else {
                return -1;
            }
        } else if (nfd == -1) {
            if (errno != EINTR)
                return -1;
        } else if (nfd == 0) {    // the situation of the poll's timeout
            if (errno != EINTR)
                return -2;
        }
        pthread_testcancel();
    }
    return n - nleft;
}

/*
 * Write n bytes to a socket.
 */
ssize_t writen(int fd, void *vptr, size_t n)
{
    ssize_t nleft, nwritten;
    const char *ptr;
    struct pollfd ufd;
    int nfd;

    ptr = vptr;
    nleft = n;
    ufd.fd = fd;
    ufd.events = POLLOUT;
    ufd.revents = 0;
    while (nleft > 0) {
        nfd = poll(&ufd, 1, 100);
        if (nfd == 1) {
            if (ufd.revents & POLLOUT) {
                if ((nwritten = write(fd, ptr, nleft)) <= 0) {
                    if (errno == EINTR)
                        nwritten = 0;
                    else
                        return -1;
                }
                nleft -= nwritten;
                ptr += nwritten;
            } else {
                return -1;
            }
        } else if (nfd == -1) {
            if (errno != EINTR)
                return -1;
        } else if (nfd == 0) {    // To see the readn()
            if (errno != EINTR)
                return -2;
        }
        pthread_testcancel();
    }
    return n;
}

/*
 * Read one line from a socket.
 */
ssize_t readline(int fd, void *vptr, size_t maxlen)
{
    ssize_t n, rc;
    char c, *ptr;

    ptr = vptr;
    for (n = 1; n < maxlen; ++n) {
    again:
        if ((rc = read(fd, &c, 1)) == 1) {
            *ptr++ = c;
            if (c == '\n')
                break;
        } else if (rc == 0) {
            break;
        } else {
            return -1;
/* we shouldn't receive a signal when reading from a socket. */
        }
    }
    if (n == 1) {
        return -1;
    } else {
        *ptr = '\0';
        return n;
    }
}