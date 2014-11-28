/*
 * Copyright (c) SOHU INC.
 * Author: Chen Yanjie <yanjiechen@sohu-inc.com>
 *
 * This file contains log functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <syslog.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include "log.h"
#include "conf.h"

#define LOG_LINE_LEN 1024

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int log_fd;
static int log_opened = 0;
static int log_pided = 0;
static int log_buf_size = 0;
static char *log_buf;
static char *log_ptr;
static time_t log_timestamp;
int log_switch = 0;

/*
 * Open the log.
 */
void log_open(void)
{
    char *log_file;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    pthread_mutex_lock(&log_mutex);
    if (!log_opened) {
        log_buf_size = option_get_int("logbufsize");
        log_buf = (char *)malloc(log_buf_size);
        if (log_buf == NULL) {
            printf("csync: not enough memory\n");
            exit(1);
        }
        log_file = option_get_str("logfile");
        if (log_file[0] == '|') {
            int to_log_fds[2];
            pid_t logpid;
            if (pipe(to_log_fds)) {
                printf("csync: can't create pipe\n");
                exit(1);
            }
            switch (logpid = fork()) {
                case 0:
                    close(STDIN_FILENO);
                    dup2(to_log_fds[0], STDIN_FILENO);
                    close(to_log_fds[0]);
                    close(to_log_fds[1]);
                    int i;
                    for (i = 3; i < 256; ++i) {
                        close(i);
                    }
                    execl("/bin/sh", "sh", "-c", log_file + 1, (char *)NULL);
                    printf("csync: spawning log-process failed\n");
                    exit(1);
                    break;
                case -1:
                    printf("csync: fork failed\n");
                    exit(1);
                    break;
                default:
                    close(to_log_fds[0]);
                    log_fd = to_log_fds[1];
                    log_pided = 1;
                    break;
            }
        } else {
            log_fd = open(log_file, O_CREAT | O_WRONLY | O_APPEND, 0644);
            if (log_fd == -1) {
                printf("csync: can't open logfile: %s\n", log_file);
                exit(1);
            }
        }
        log_ptr = log_buf;
        log_opened = 1;
        time(&log_timestamp);
    }
    pthread_mutex_unlock(&log_mutex);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
}

/*
 * Close the log.
 */
void log_close(void)
{
    ssize_t size;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    pthread_mutex_lock(&log_mutex);
    if (log_opened) {
        size = log_ptr - log_buf;
        if (size > 0) {
            write(log_fd, log_buf, size);
        }
        close(log_fd);
        free(log_buf);
        log_opened = 0;
    }
    pthread_mutex_unlock(&log_mutex);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
}

/*
 * Reopen log file, should be protected by log_mutex.
 */
void log_reopen(void)
{
    if (!log_pided) {
        char *log_file;

        write(log_fd, log_buf, log_ptr - log_buf);
        log_ptr = log_buf;
        close(log_fd);
        log_file = option_get_str("logfile");
        log_fd = open(log_file, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (log_fd == -1) {
            printf("csync: can't open logfile\n");
            exit(1);
        }
    }
}

/*
 * Append a string buffer to the log.
 */
void log_msg(int log_level, const char *format, ...)
{
    ssize_t wrote;
    ssize_t len;
    time_t cur_time;
    struct tm tm;
    char *asc_time;
    char buf[4096], *p;
    va_list arglist;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    pthread_mutex_lock(&log_mutex);
    if (log_switch) {
        log_reopen();
        log_switch = 0;
    }
    if (log_opened) {
        time(&cur_time);
        localtime_r(&cur_time, &tm);
        strftime(buf, sizeof(buf), "%F %T", &tm);
        len = strlen(buf);
        p = buf + len;
        switch(log_level) {
        case LOG_INFO:
            snprintf(p, len, " INFO: ");
            break;
        case LOG_ERR:
            snprintf(p, len, " ERROR: ");
            break;
        case LOG_DEBUG:
            snprintf(p, len, " DEBUG: ");
            break;
        default:
            break;
        }
        len = strlen(buf);
        p = buf + len;
        len = sizeof(buf) - len;
        va_start(arglist, format);
        vsnprintf(p, len, format, arglist);
        va_end(arglist);
        len = strlen(buf);
        if (sizeof(buf) - len > 1) {
            strcat(buf, "\n");
            ++len;
        }
        if ((len >= (log_buf + log_buf_size - log_ptr)) ||
            ((cur_time - log_timestamp) > 10)) {
            write(log_fd, log_buf, log_ptr - log_buf);
            log_ptr = log_buf;
            strncpy(log_ptr, buf, len);
            log_ptr += len;
            log_timestamp = cur_time;
        } else {
            strncpy(log_ptr, buf, len);
            log_ptr += len;
        }
    }
    pthread_mutex_unlock(&log_mutex);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
}