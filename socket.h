/*
 * Copyright (c) SOHU INC.
 * Author: Chen Yanjie <yanjiechen@sohu-inc.com>
 */

#ifndef _SOCKET_H
#define _SOCKET_H

#ifdef _DEBUG
typedef enum {
    TYPE_NONE, TYPE_FLAG, TYPE_INT, TYPE_LINGER, TYPE_TIMEVAL, TYPE_STRING
} VAL_TYPE;

typedef union {
    int i_val;
    long l_val;
    char c_val[16];
    struct linger linger_val;
    struct timeval timeval_val;
} OPT_UNION;

typedef struct {
    char *opt_str;
    int opt_level;
    int opt_name;
    VAL_TYPE opt_type;
    OPT_UNION *opt_val[3];
} SOCK_OPT;

void print_socket_options(void);
static void print_option(char *, int, OPT_UNION *val);
#endif

int server_socket(char *, char *);
int client_socket(char *, char *, pthread_t);
ssize_t readn(int, void *, size_t);
ssize_t writen(int, void *, size_t);
ssize_t readline(int, void *, size_t);

#endif