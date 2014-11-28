/*
 * Copyright (c) SOHU INC.
 * Author: Chen Yanjie <yanjiechen@sohu-inc.com>
 *
 * This file include the main server and client functions.
 */

#define _XOPEN_SOURCE

#include <strings.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <syslog.h>
#include <sys/time.h>
#include <time.h>
#include <utime.h>
#include <pwd.h>
#include <grp.h>
#include "ght_hash_table.h"
#include "csync.h"
#include "log.h"
#include "conf.h"
#include "lock.h"
#include "str.h"

char *version = "csync version 1.4.3";

char *mode;
char *syncroot;
char *rsyncroot;
char *listtype;
char *listfile;
char *switchfile;
char *archdir;
char *savefiledir;
int checkinterval;
int archinterval;
int timeout;
char *preservetimes;
int promisc;
int truncsize;
int maxqueuesize;
int catdirdepth;
unsigned long startfrom;
time_t current_timestamp;
time_t switch_timestamp;
ght_hash_table_t *categories;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t mutex_tid;
unsigned int mutex_count;
FILE *arcfp;
time_t current_arc;
pthread_t list_server_tid;
pthread_t watchdog_server_tid;
pthread_t sync_server_listener_tid;
sync_server_list_t sync_server_list;
sync_client_list_t sync_client_list;
pthread_t csync_exit_thread = 0;
int csync_exit_status = 0;
extern int log_switch;

#ifdef _DEBUG

void print_command(command *com)
{
    if (com != NULL) {
        if (com->op == '*') {
            if (com->size != 0) {
                log_msg(LOG_DEBUG, "%u %s %c %s %s %u", com->timestamp,
                        com->cat, com->op, com->arg0, com->arg1, com->size);
            } else {
                log_msg(LOG_DEBUG, "%u %s %c %s %s", com->timestamp, com->cat,
                        com->op, com->arg0, com->arg1);
            }
        } else {
            if (com->size != 0) {
                log_msg(LOG_DEBUG, "%u %s %c %s %u", com->timestamp, com->cat,
                        com->op, com->arg0, com->size);
            } else {
                log_msg(LOG_DEBUG, "%u %s %c %s", com->timestamp, com->cat,
                        com->op, com->arg0);
            }
        }
    } else
        log_msg(LOG_DEBUG, "null command");
}

void print_category(category *cat)
{
    if (cat == NULL) {
        log_msg(LOG_DEBUG, "null category");
        return;
    }
    log_msg(LOG_DEBUG, "%s(%u)", cat->name, cat->count);
}

log_stamp(char *s)
{
    struct timeval tv1;
    double clock1;

    gettimeofday(&tv1, NULL);
    clock1 = (double)tv1.tv_sec + (double)tv1.tv_usec / 1000000;
    log_msg(LOG_INFO, "%s(%u): %lf", s, pthread_self(), clock1);
}
#endif

/*
 * Set current timestamp.
 */
void set_current_timestamp(time_t t)
{
    if (t > current_timestamp)
        current_timestamp = t;
}

/*
 * Set switch timestamp.
 */
void set_switch_timestamp(time_t t)
{
    if (t > switch_timestamp)
        switch_timestamp = t;
}

/*
 * Get current timestamp.
 */
time_t get_current_timestamp(void)
{
    return current_timestamp;
}

/*
 * Get switch timestamp.
 */
time_t get_switch_timestamp(void)
{
    return switch_timestamp;
}

/*
 * Parse one command line. the command format is in csync's format,
 * not cms format or ftp format; they must have been translated to
 * csync's format. look into the archive log file to get the format.
 */
int parse_command(command *com, char *line)
{
    int len, i, count;
    char *p, *pp, *ppp;
    time_t timestamp;
    size_t flen = 0;
    char buf[MAX_COM_LEN], op, *cat, *arg0, *arg1, *size;

    strncpy(buf, line, MAX_COM_LEN);
    buf[MAX_COM_LEN - 1] = '\0';
    p = strtrim(buf);
    len = strlen(p);
    pp = p;

    while (isdigit(*pp)) /* the timestamp */
        pp++;
    if ((*p == '\0') || (*pp == '\0'))
        return 1;
    *pp = '\0';
    timestamp = strtoul(p, &ppp, 10);
    if ((*ppp != 0) || (timestamp == 0) || (timestamp == ULONG_MAX) || (timestamp < 1300000000) || (timestamp > 1577807999))
        return 1;
    pp++;
    while (isspace(*pp))
        pp++;
    if (*pp == '\0')
        return 1;

    /* the category */
    cat = pp;
    while ((*pp != '\0') && (*pp != '\t'))
        pp++;
    if (*pp == '\0')
        return 1;
    *pp = '\0';
    pp++;

    op = *pp; /* the operator */
    if ((op != '+') && (op != '-') && (op != '*'))
        return 1;
    pp++;
    while (isspace(*pp))
        pp++;
    if (*pp == '\0')
        return 1;

    arg0 = pp; /* arg0 */
    while ((*pp != '\0') && (*pp != '\t'))
        pp++;
    *pp = '\0';
    pp++;

    if (op == '*') { /* arg1 */
        if ((pp - p) >= len)
            return 1;
        while (isspace(*pp))
            pp++;
        if (*pp == '\0')
            return 1;
        arg1 = pp;
        while ((*pp != '\0') && (*pp != '\t'))
            pp++;
        *pp = '\0';
    } else if (op == '+') {
        if ((pp - p) < len) { /* the length of arg0, if the command has */
            while (isspace(*pp))
                pp++;
            size = pp;
            while (isdigit(*pp))
                pp++;
            *pp = '\0';
            if (*size != '\0') {
                flen = strtoul(size, &ppp, 10);
                if ((*ppp != 0) || (flen == 0) || (flen == ULONG_MAX))
                    return 1;
            }
        }
    }

    com->timestamp = timestamp;
    strncpy(com->cat, cat, MAX_CAT_LEN);
    com->cat[MAX_CAT_LEN - 1] = '\0';
    com->op = op;
    strncpy(com->arg0, arg0, MAX_COM_LEN);
    com->arg0[MAX_COM_LEN - 1] = '\0';
    if (op == '*') {
        com->size = 0;
        strncpy(com->arg1, arg1, MAX_COM_LEN);
        com->arg1[MAX_COM_LEN - 1] = '\0';
    } else if (op == '+') {
        com->size = flen;
        com->arg1[0] = '\0';
    } else if (op == '-') {
        com->size = 0;
        com->arg1[0] = '\0';
    }
    return 0;
}

/*
 * Parse one cms command line. translate it to csync's format.
 */
int parse_cms_command(command *com, char *line)
{
    int comlen, catlen, i, count;
    char *p, *pp, *ppp;
    time_t timestamp;
    size_t flen = 0;
    char buf[MAX_COM_LEN], *cat, op, *arg0, *arg1, *size;

    strncpy(buf, line, MAX_COM_LEN);
    buf[MAX_COM_LEN - 1] = '\0';
    p = strtrim(buf);
    comlen = strlen(p);
    pp = p;

    while (isdigit(*pp)) /* the timestamp */
        pp++;
    if ((*p == '\0') || (*pp == '\0'))
        return 1;
    *pp = '\0';
    timestamp = strtoul(p, &ppp, 10);
    if ((*ppp != 0) || (timestamp == 0) || (timestamp == ULONG_MAX) || (timestamp < 1300000000) || (timestamp > 1577807999))
        return 1;
    pp++;
    while (isspace(*pp))
        pp++;
    if (*pp == '\0')
        return 1;

    op = *pp; /* the operator */
    if ((op != '+') && (op != '-') && (op != '*'))
        return 1;
    pp++;
    while (isspace(*pp))
        pp++;
    if (*pp == '\0')
        return 1;

    catlen = strlen(syncroot);
    if ((op == '+') || (op == '-')) {
        if (strncmp(syncroot, pp, catlen))
            return 1;
        pp += catlen;
        while (*pp == '/')
            pp++;
        if (*pp == '\0')
            return 1;

        cat = pp; /* category */
        while ((*pp != '/') && (*pp != '\0'))
            pp++;
        if (*pp == '\0')
            return 1;
        *pp = '\0';
        pp++;
        while (*pp == '/')
            pp++;
        if (*pp == '\0')
            return 1;

        arg0 = pp; /* arg0 */
        while ((*pp != '\0') && (*pp != '\t'))
            pp++;
        *pp = '\0';
        pp++;

        if ((op == '+') && ((pp - p) < comlen)) { /* the length of file */
            while (isspace(*pp))
                pp++;
            if (*pp == '\0')
                return 1;
            size = pp;
            while (isdigit(*pp))
                pp++;
            *pp = '\0';
            flen = strtoul(size, &ppp, 10);
            if ((*ppp != 0) || (flen == 0) || (flen == ULONG_MAX))
                return 1;
        }
    } else if (op == '*') {
        arg0 = pp; /* arg0 */
        while ((*pp != '\0') && (*pp != '\t'))
            pp++;
        if (*pp == '\0')
            return 1;
        *pp = '\0';
        pp++;
        while (isspace(*pp))
            pp++;
        if (*pp == '\0')
            return 1;

        if (strncmp(syncroot, pp, catlen))
            return 1;
        pp += catlen;
        while (*pp == '/')
            pp++;
        if (*pp == '\0')
            return 1;

        cat = pp; /* category */
        while ((*pp != '/') && (*pp != '\0'))
            pp++;
        if (*pp == '\0')
            return 1;
        *pp = '\0';
        pp++;
        while (*pp == '/')
            pp++;
        if (*pp == '\0')
            return 1;

        arg1 = pp; /* arg1 */
        while ((*pp != '\0') && (*pp != '\t'))
            pp++;
        *pp = '\0';
    }
    pp++;

    com->timestamp = timestamp;
    strncpy(com->cat, cat, MAX_CAT_LEN);
    com->cat[MAX_CAT_LEN - 1] = '\0';
    com->op = op;
    strncpy(com->arg0, arg0, MAX_COM_LEN);
    com->arg0[MAX_COM_LEN - 1] = '\0';
    if (op == '*') {
        com->size = 0;
        strncpy(com->arg1, arg1, MAX_COM_LEN);
        com->arg1[MAX_COM_LEN - 1] = '\0';
    } else if (op == '+') {
        com->size = flen;
        com->arg1[0] = '\0';
    } else if (op == '-') {
        com->size = 0;
        com->arg1[0] = '\0';
    }
    return 0;
}

/*
 * parse one ftp command. translate it to csync's format.
 */
int parse_ftp_command(command *com, char *line)
{
    int i, count, len;
    char current_time[25], filename[MAX_COM_LEN], direction, *p, *cat;
    struct tm tm;

    len = strlen(line);
    if (len < 49)
        return 1;
    strncpy(current_time, line, 25);
    current_time[24] = '\0';
    p = line + 25;
    while ((*p != ' ') && (*p != '\0')) /* skip transfer time */
        p ++;
    if (*p == '\0')
        return 1;
    else
        p ++;
    while ((*p != ' ') && (*p != '\0')) /* skip client address */
        p ++;
    if (*p == '\0')
        return 1;
    else
        p ++;
    while ((*p != ' ') && (*p != '\0')) /* skip file size */
        p ++;
    if (*p == '\0')
        return 1;
    else
        p ++;
    i = 0;
    while ((*p != ' ') && (*p != '\0')) { /* file name */
        filename[i++] = *p++;
        if (i >= (sizeof(filename) - 1))
            return 1;
    }
    filename[i] = '\0';
    if (*p == '\0')
        return 1;
    else
        p ++;
    while ((*p != ' ') && (*p != '\0')) /* skip transfer type */
        p ++;
    if (*p == '\0')
        return 1;
    else
        p ++;
    while ((*p != ' ') && (*p != '\0')) /* skip special action flag */
        p ++;
    if (*p == '\0')
        return 1;
    else
        p ++;
    if (*p == 'i') /* direction */
        direction = '+';
    else if (*p == 'd')
        direction = '-';
    else
        return 2;
    com->op = direction;
    if (strptime(current_time, "%a%n%b%n%d%n%H:%M:%S%n%Y", &tm) == NULL)
        return 1;
    if ((com->timestamp = mktime(&tm)) == -1)
        return 1;
    len = strlen(syncroot);
    if (strncmp(syncroot, filename, len))
        return 2;
    p = filename + len;
    while (*p == '/')
        p++;
    if (*p == '\0')
        return 1;
    cat = p;
    while ((*p != '/') && (*p != '\0'))
        p++;
    if (*p == '\0')
        return 1;
    *p = '\0';
    p++;
    while (*p == '/')
        p++;
    strncpy(com->cat, cat, sizeof(com->cat));
    com->cat[sizeof(com->cat)-1] = '\0';
    strncpy(com->arg0, p, sizeof(com->arg0));
    com->arg0[sizeof(com->arg0)-1] = '\0';
    return 0;
}

/*
 * Archive a command to log file.
 */
int arc_command(command *com)
{
    int len, fd;
    time_t arc;
    char path[256];
    char command[MAX_COM_LEN];
    struct tm tm;

    if (archdir == NULL)
        return 0;
    arc = (com->timestamp / archinterval) * archinterval;
    if (localtime_r(&arc, &tm) == NULL)
        return -1;
    snprintf(path, sizeof(path), "%s/arc_%s", archdir, com->cat);
    len = strlen(path);
    if (strftime(path + len, sizeof(path) - len, "-%Y%m%d.log", &tm) == 0)
        return -1;
    if (com->op != '*')
        snprintf(command, MAX_COM_LEN, "%u\t%s\t%c\t%s\n", com->timestamp,
                 com->cat, com->op, com->arg0);
    else
        snprintf(command, MAX_COM_LEN, "%u\t%s\t%c\t%s\t%s\n", com->timestamp,
                 com->cat, com->op, com->arg0, com->arg1);
    if ((fd = open(path, O_CREAT | O_WRONLY | O_APPEND,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
        return -1;
    lock_file(fd, F_WRLCK);
    len = write(fd, command, strlen(command));
    unlock_file(fd);
    close(fd);
    if (len <= 0)
        return -1;
    else
        return 0;
}

/*
 * Duplicate one command structure.
 */
command *dup_command(command *com)
{
    command *new_com;

    new_com = (command *)malloc(sizeof(command));
    if (new_com == NULL)
        return NULL;
    memcpy(new_com, com, sizeof(command));
    return new_com;
}

/*
 * Free a command structure.
 */
void free_command(command *com)
{
    free(com);
}

/*
 * Convert a command to string. used for logging.
 */
void to_string(command *com, char *buf, ssize_t len)
{
    if (com != NULL) {
        if (com->op == '*') {
            if (com->size != 0) {
                snprintf(buf, len, "%u %s %c %s %s %u", com->timestamp,
                        com->cat, com->op, com->arg0, com->arg1, com->size);
            } else {
                snprintf(buf, len, "%u %s %c %s %s", com->timestamp, com->cat,
                        com->op, com->arg0, com->arg1);
            }
        } else {
            if (com->size != 0) {
                snprintf(buf, len, "%u %s %c %s %u", com->timestamp, com->cat,
                        com->op, com->arg0, com->size);
            } else {
                snprintf(buf, len, "%u %s %c %s", com->timestamp, com->cat,
                        com->op, com->arg0);
            }
        }
    } else
        snprintf(buf, len, "null command");
}

/*
 * Free a category structure.
 */
void free_category(category *cat)
{
    command *com, *del;
    category *cats;

    if (cat == NULL)
        return;
    for (com = cat->head; com != NULL;) {
        del = com;
        com = com->next;
        free_command(del);
    }
    if ((cat->prev == NULL) && (cat->next == NULL)) {
        cats = (category *)ght_get(categories, strlen(cat->name), cat->name);
        if (cats == cat)
            ght_remove(categories, strlen(cat->name), cat->name);
    } else if (cat->prev == NULL) {
        cats = (category *)ght_get(categories, strlen(cat->name), cat->name);
        if (cats == cat)
            ght_remove(categories, strlen(cat->name), cat->name);
        cats = cat->next;
        cat->next->prev = NULL;
        ght_insert(categories, cats, strlen(cat->name), cat->name);
    } else if (cat->next == NULL) {
        cat->prev->next = NULL;
    } else {
        cat->prev->next = cat->next;
        cat->next->prev = cat->prev;
    }
    free(cat);
}

/*
 * Add one command to command list of each registered category.
 */
void add_command(command *comm, int where)
{
    int i, count = 0;
    char cat_name[MAX_CAT_LEN];
    category *cat;
    command *com;

    cat = (category *)ght_get(categories, strlen(comm->cat), comm->cat);
    if (cat == NULL) {
        return;
    }
    do {
        com = dup_command(comm);
        if (cat->count == 0) {
            com->next = NULL;
            cat->head = com;
            cat->tail = com;
        } else {
            if (where == 0) {
                com->next = cat->head;
                cat->head = com;
            } else {
                com->next = NULL;
                cat->tail->next = com;
                cat->tail = com;
            }
        }
        cat->count++;
#ifdef _DEBUG
        print_category(cat);
#endif
    } while ((cat = cat->next) != NULL);
}

/*
 * Add a category to hashtable.
 */
void add_category(category *cat, int where)
{
    category *c;
    int len;

    len = strlen(cat->name);
    c = ght_get(categories, len, cat->name);
    if (c == NULL) {
        cat->prev = NULL;
        cat->next = NULL;
        ght_insert(categories, cat, len, cat->name);
    } else {
        if (where == 0) {
            cat->prev = NULL;
            cat->next = c;
            c->prev = cat;
            ght_remove(categories, len, cat->name);
            ght_insert(categories, cat, len, cat->name);
        } else {
            while (c->next != NULL)
                c = c->next;
            c->next = cat;
            cat->prev = c;
            cat->next = NULL;
        }
    }
}

/*
 * Get a command from a category.
 */
command *get_command(category *cat, int where)
{
    pthread_t tid;
    command *com = NULL;

    tid = pthread_self();
    if (cat == NULL) {
        return NULL;
    }
#ifdef _DEBUG
    print_category(cat);
#endif
    if (cat->count == 0) {
        com = NULL;
    } else if (cat->count == 1) {
        com = cat->head;
        cat->head = NULL;
        cat->tail = NULL;
        cat->count--;
    } else {
        com = cat->head;
        cat->head = com->next;
        cat->count--;
    }
    return com;
}

/*
 * Update the lastcomm timestamp.
 */
void update_lastcomm(time_t *t)
{
    *t = time(NULL);
}

/*
 * Send a command to sync client.
 */
int send_command(sync_server_t *pth, command *com, char *transmod)
{
    char buf[MAX_COM_LEN];
    ssize_t n, len;
    off_t filelen;
    int fd, ffd;
    char fbuf[8192];
    struct stat st;

    update_lastcomm(&pth->lastcomm);
    fd = pth->fd;
    switch (com->op) {
    case '+': /* add file or directory */
        snprintf(buf, sizeof(buf), "%s/%s/%s", syncroot, com->cat, com->arg0);
        /* deal with 0 byte file */
        openagain:
        ffd = open(buf, O_RDONLY);
        if (ffd < 0) {
            log_msg(LOG_ERR, "send_command: can't open file: %s", buf);
            return -1;
        }
        if (fstat(ffd, &st) != 0) {
            log_msg(LOG_ERR, "send_command: can't fstat file: %s", buf);
            return -1;
        }
        if (!S_ISREG(st.st_mode)) {
            log_msg(LOG_ERR, "send_command: %s is not a regular file", buf);
            return -1;
        }
        filelen = lseek(ffd, 0, SEEK_END);
        if (filelen == -1) {
            close(ffd);
            log_msg(LOG_ERR, "send_command: can't lseek file: %s", buf);
            return -1;
        }
        /* deal with 0 byte file */
        if (filelen == 0) {
            close(ffd);
            ffd = open(buf, O_RDWR);
            if (ffd < 0) {
                log_msg(LOG_ERR, "send_command: can't open 0 byte file READWRITE: %s", buf);
            } else {
                write(ffd,"\n",1);
                close(ffd);
                goto openagain;
            }
        }

        com->size = filelen;
        snprintf(buf, MAX_COM_LEN, "%lu\t%s\t%c\t%s\t%lu\n", com->timestamp,
                 com->cat, com->op, com->arg0, com->size);
        len = strlen(buf);
        if (writen(fd, buf, len) != len) {
            close(ffd);
            log_msg(LOG_ERR, "send_command: can't send to peer: %s", buf);
            return -2;
        }
        update_lastcomm(&pth->lastcomm);
    if (!strcasecmp(transmod, "shell")) {
        close(ffd);
    } else {
        lseek(ffd, 0, SEEK_SET);
        while ((n = read(ffd, fbuf, sizeof(fbuf))) > 0) {
            filelen -= n;
            if (filelen < 0) {
                close(ffd);
                log_msg(LOG_ERR, "send_command: file reading error: %d",
                        filelen);
                return -2;
            }
            if (writen(fd, fbuf, n) != n) {
                close(ffd);
                log_msg(LOG_ERR, "send_command: socket writing error");
                return -2;
            }
            update_lastcomm(&pth->lastcomm);
        }
        close(ffd);
        if (filelen != 0) {
            log_msg(LOG_ERR, "send_command: error sending command: %d",
                    filelen);
            return -2;
        }
    }
        break;
    case '-': /* remove file or directory */
        snprintf(buf, MAX_COM_LEN, "%lu\t%s\t%c\t%s\n", com->timestamp,
                 com->cat, com->op, com->arg0);
        len = strlen(buf);
        if (writen(fd, buf, len) != len) {
            log_msg(LOG_DEBUG, "send_command: can't send to peer: %s", buf);
            return -2;
        }
        break;
    case '*': /* link file */
        snprintf(buf, MAX_COM_LEN,"%lu\t%s\t%c\t%s\t%s\n", com->timestamp,
                 com->cat, com->op, com->arg0, com->arg1);
        len = strlen(buf);
        if (writen(fd, buf, len) != len) {
            log_msg(LOG_DEBUG, "send_command: can't send to peer: %s", buf);
            return -2;
        }
        break;
    default:
        return -1;
    }
    update_lastcomm(&pth->lastcomm);
    return 0;
}

/*
 * Make parent directory of a full path file.
 */
int mkpdir(char *path)
{
    char *p, *pp;
    char buf[MAX_COM_LEN];
    struct stat st;

    p = rindex(path, '/');
    if (p == NULL)
        return -1;
    strncpy(buf, path, p - path + 1);
    buf[p - path + 1] = '\0';
    if (stat(buf, &st) == 0)
        return 0;
    p = buf + 1;
    while ((pp = index(p, '/')) != NULL) {
        *pp = '\0';
        if (stat(buf, &st) != 0) {
            if (mkdir(buf, 0755) != 0) {
                if (errno != EEXIST)
                    return -1;
            }
        }
        *pp = '/';
        p = pp + 1;
    }
    return 0;
}

/*
 * Receive and process a command.
 */
int receive_command(sync_client_t *pth, command *com, char *category, char *transmod,
            char *shellcom, char *shellrem, char *shellsym)
{
    char path[MAX_COM_LEN], tmpath[MAX_COM_LEN], fullshellcom[MAX_COM_LEN];
    char rsyncpath[MAX_COM_LEN];
    char *p;
    int fd, ffd, count, left, n;
    char rbuf[8192];

    update_lastcomm(&pth->lastcomm);
    fd = pth->fd;
    switch (com->op) {
    case '+':
        snprintf(path, sizeof(path), "%s/%s/%s",
                 syncroot, com->cat, com->arg0);
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "local path: %s", path);
#endif
    if (!strcasecmp(transmod, "shell")) {
        if ((shellcom != NULL) && (strlen(shellcom) != 0)) {
            snprintf(fullshellcom, sizeof(fullshellcom), "%s %s %lu %c >/dev/null 2>/dev/null &",
                 shellcom, path, com->size, com->op);
            if (system(fullshellcom) == 0) {
                log_msg(LOG_INFO, "receive_command: execute command \'%s\'", fullshellcom);
                update_lastcomm(&pth->lastcomm);
                return 0;
            } else {
                log_msg(LOG_ERR, "receive_command: can't execute command \'%s\'", fullshellcom);
                return -2;
            }
        } else {
            log_msg(LOG_ERR, "receive_command: the shell command is null");
            return -2;
        }
    } else {
        strncpy(tmpath, path, MAX_COM_LEN);
        if (mkpdir(path) != 0) {
            log_msg(LOG_ERR, "receive_command: can't mkpdir: %s", path);
            return -2;
        }
        p = rindex(tmpath, '/');
        if (p == NULL)
            p = tmpath;
        snprintf(p, MAX_COM_LEN - (p - tmpath), "/.csync_XXXXXX");
        if ((ffd = mkstemp(tmpath)) == -1) {
            log_msg(LOG_ERR, "receive_command: can't mkstemp: %s", tmpath);
            return -2;
        }
        count = com->size / sizeof(rbuf);
        left = com->size % sizeof(rbuf);
        while (count-- > 0) {
            n = readn(fd, rbuf, sizeof(rbuf));
            if (n != sizeof(rbuf)) {
                close(ffd);
                unlink(tmpath);
                log_msg(LOG_ERR, "receive_command: can't read %d bytes from socket. buffer %d, count %d, got %d.", com->size, sizeof(rbuf), count, n);
                return -2;
            } else {
                n = write(ffd, rbuf, sizeof(rbuf));
                if (n != sizeof(rbuf)) {
                    close(ffd);
                    unlink(tmpath);
                    log_msg(LOG_ERR,
                            "receive_command: can't write to file: %s",
                            tmpath);
                    return -2;
                }
            }
            update_lastcomm(&pth->lastcomm);
        }
        if (left > 0) {
            n = readn(fd, rbuf, left);
            if (n != left) {
                close(ffd);
                unlink(tmpath);
                log_msg(LOG_ERR, "receive_command: can't read %d bytes from socket. got %d", left, n);
                return -2;
            } else {
                n = write(ffd, rbuf, left);
                if (n != left) {
                    close(ffd);
                    unlink(tmpath);
                    log_msg(LOG_ERR,
                            "receive_command: can't write to file: %s",
                            tmpath);
                    return -2;
                }
            }
            update_lastcomm(&pth->lastcomm);
        }
        close(ffd);
        if (rename(tmpath, path) != 0) {
            log_msg(LOG_ERR, "receive_command: can't rename file %s to %s",
                    tmpath, path);
            unlink(tmpath);
            return -2;
        } else {
            chmod(path, 0644);
            if (!strcasecmp("on", preservetimes) || !strcasecmp("true", preservetimes)
                || !strcasecmp("yes", preservetimes)) {
                struct utimbuf tbuf;
                tbuf.actime = time(NULL);
                tbuf.modtime = com->timestamp;
                if (utime(path, &tbuf)) {
                    log_msg(LOG_ERR, "receive_command: can't set times on file %s",
                        path);
                }
            }
            return 0;
        }
    }
        break;
    case '-':
        snprintf(path, sizeof(path), "%s/%s/%s",
                 syncroot, com->cat, com->arg0);
    if (!strcasecmp(transmod, "shell")) {
        if ((shellrem != NULL) && (strlen(shellrem) != 0)) {
            snprintf(fullshellcom, sizeof(fullshellcom), "%s %s %lu %c >/dev/null 2>/dev/null &",
                 shellrem, path, com->size, com->op);
            if (system(fullshellcom) == 0) {
                log_msg(LOG_INFO, "receive_command: execute command \'%s\'", fullshellcom);
                update_lastcomm(&pth->lastcomm);
            } else {
                log_msg(LOG_ERR, "receive_command: can't execute command \'%s\'", fullshellcom);
            }
        }
    }
        if ((rsyncroot != NULL) && (strlen(rsyncroot) != 0)) {
            snprintf(rsyncpath, sizeof(rsyncpath), "%s/%s/%s",
                     rsyncroot, com->cat, com->arg0);
            if (unlink(rsyncpath) != 0) {
                log_msg(LOG_ERR, "receive_command: can't unlink rsync file: %s", rsyncpath);
            }
        }

        if (unlink(path) == 0)
            return 0;
        else {
            log_msg(LOG_ERR, "receive_command: can't unlink file: %s", path);
            return -1;
        }
        break;
    case '*':
        snprintf(path, sizeof(path), "%s/%s/%s",
                 syncroot, com->cat, com->arg1);
    if (!strcasecmp(transmod, "shell")) {
        if ((shellsym != NULL) && (strlen(shellsym) != 0)) {
            snprintf(fullshellcom, sizeof(fullshellcom), "%s %s %lu '%c' >/dev/null 2>/dev/null &",
                 shellsym, path, com->size, com->op);
            if (system(fullshellcom) == 0) {
                log_msg(LOG_INFO, "receive_command: execute command \'%s\'", fullshellcom);
                update_lastcomm(&pth->lastcomm);
            } else {
                log_msg(LOG_ERR, "receive_command: can't execute command \'%s\'", fullshellcom);
            }
        }
    }
        if (mkpdir(path) != 0) {
            log_msg(LOG_ERR, "receive_command: can't mkpdir: %s", path);
            return -2;
        }
        if (symlink(com->arg0, path) != 0) {
            log_msg(LOG_ERR, "receive_command: can't symlink file %s to %s",
                    com->arg0, path);
            if (errno == EEXIST)
                return 0;
            else
                return -2;
        } else
            return 0;
        break;
    default:
        return -1;
    }
}

/*
 * Get a category from hashtable.
 */
category *get_category(char *name)
{
    category *cat;
    pthread_t tid;

    tid = pthread_self();
    cat = (category *)ght_get(categories, strlen(name), name);
    while (cat != NULL) {
        if (cat->tid != tid)
            cat = cat->next;
        else
            break;
    }
    return cat;
}

/*
 * Create a new category structure.
 */
category *new_category(char *name)
{
    category *cat;
    pthread_t tid;

    tid = pthread_self();
    cat = (category *)malloc(sizeof(category));
    if (cat == NULL)
        return NULL;
    strncpy(cat->name, name, MAX_CAT_LEN);
    cat->name[MAX_CAT_LEN - 1] = '\0';
    cat->tid = tid;
    cat->count = 0;
    cat->head = NULL;
    cat->tail = NULL;
    cat->prev = NULL;
    cat->next = NULL;
    return cat;
}

/*
 * Read the sync timestamp from savefiledir.
 */
time_t read_timestamp(char *category)
{
    FILE *fp;
    char path[256];
    char rbuf[256];
    char *p;
    time_t last = 0;

    snprintf(path, sizeof(path), "%s/timestamp_%s.sav", savefiledir, category);
    fp = fopen(path, "r");
    if (fp == NULL) {
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "read_timestamp(%u): can't open file: %s",
                pthread_self(), path);
#endif
        return 0;
    }
    lock_file(fileno(fp), F_RDLCK);
    if (fgets(rbuf, sizeof(rbuf), fp) == NULL) {
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "read_timestamp(%u): can't read savefile: %s",
                pthread_self(), path);
#endif
    } else {
        last = strtoul(strtrim(rbuf), &p, 10);
        if (*p != 0) {
#ifdef _DEBUG
            log_msg(LOG_ERR, "read_timestamp(%u): timestamp error: %s",
                    pthread_self(), rbuf);
#endif
            last = 0;
        }
    }
    unlock_file(fileno(fp));
    fclose(fp);
    return last;
}

/*
 * Save the sync timestamp to file.
 */
int save_timestamp(char *category, time_t timestamp)
{
    char path[256];
    char buf[256];
    int fd, len;

    snprintf(path, sizeof(path), "%s/timestamp_%s.sav", savefiledir, category);
    if ((fd = open(path, O_CREAT | O_WRONLY,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "save_timestamp(%u): can't open file: %s",
                pthread_self(), path);
#endif
        return 1;
    }
    lock_file(fd, F_WRLCK);
    snprintf(buf, sizeof(buf), "%lu", timestamp);
    len = strlen(buf);
    if (write(fd, buf, len) != len) {
        unlock_file(fd);
        close(fd);
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "save_timestamp(%u): writing error: %s",
                pthread_self(), path);
#endif
        return 1;
    } else {
        unlock_file(fd);
        close(fd);
        return 0;
    }
}

/*
 * Read the current sync timestamp from savefiledir.
 * The current sync timestamp is only valid in server mode.
 * It's the timestamp from listfile.
 */
time_t read_current_timestamp(void)
{
    FILE *fp;
    char path[256];
    char rbuf[256];
    char *p;
    time_t last = 0;

    snprintf(path, sizeof(path), "%s/timestamp.sav", savefiledir);
    fp = fopen(path, "r");
    if (fp == NULL) {
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "read_current_timestamp(%u): can't open file: %s",
                pthread_self(), path);
#endif
        return 0;
    }
    if (fgets(rbuf, sizeof(rbuf), fp) == NULL) {
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "read_current_timestamp(%u): can't read file: %s",
                pthread_self(), path);
#endif
    } else {
        last = strtoul(strtrim(rbuf), &p, 10);
        if (*p != 0) {
#ifdef _DEBUG
        log_msg(LOG_ERR, "read_current_timestamp(%u): timestamp error: %s",
                pthread_self(), rbuf);
#endif
        }
    }
    fclose(fp);
    return last;
}

/*
 * Read the switch sync timestamp from savefiledir.
 * The switch sync timestamp is only valid in server mode.
 * It's the timestamp from switchfile.
 */
time_t read_switch_timestamp(void)
{
    FILE *fp;
    char path[256];
    char rbuf[256];
    char *p;
    time_t last = 0;

    snprintf(path, sizeof(path), "%s/switchtime.sav", savefiledir);
    fp = fopen(path, "r");
    if (fp == NULL) {
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "read_switch_timestamp(%u): can't open file: %s",
                pthread_self(), path);
#endif
        return 0;
    }
    if (fgets(rbuf, sizeof(rbuf), fp) == NULL) {
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "read_switch_timestamp(%u): can't read file: %s",
                pthread_self(), path);
#endif
    } else {
        last = strtoul(strtrim(rbuf), &p, 10);
        if (*p != 0) {
#ifdef _DEBUG
        log_msg(LOG_ERR, "read_switch_timestamp(%u): timestamp error: %s",
                pthread_self(), rbuf);
#endif
        }
    }
    fclose(fp);
    return last;
}

/*
 * Save the current sync timestamp to file.
 */
int save_current_timestamp(time_t t)
{
    char path[256];
    char buf[256];
    int fd, len;

    snprintf(path, sizeof(path), "%s/timestamp.sav", savefiledir);
    if ((fd = open(path, O_CREAT | O_WRONLY,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "save_current_timestamp(%u): can't open file: %s",
                pthread_self(), path);
#endif
        return 1;
    }
    snprintf(buf, sizeof(buf), "%lu", t);
    len = strlen(buf);
    if (write(fd, buf, len) != len) {
        close(fd);
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "save_current_timestamp(%u): writing error: %s",
                pthread_self(), path);
#endif
        return 1;
    } else {
        close(fd);
        return 0;
    }
}

/*
 * Save the switch sync timestamp to file.
 */
int save_switch_timestamp(time_t t)
{
    char path[256];
    char buf[256];
    int fd, len;

    snprintf(path, sizeof(path), "%s/switchtime.sav", savefiledir);
    if ((fd = open(path, O_CREAT | O_WRONLY,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "save_switch_timestamp(%u): can't open file: %s",
                pthread_self(), path);
#endif
        return 1;
    }
    snprintf(buf, sizeof(buf), "%lu", t);
    len = strlen(buf);
    if (write(fd, buf, len) != len) {
        close(fd);
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "save_switch_timestamp(%u): writing error: %s",
                pthread_self(), path);
#endif
        return 1;
    } else {
        close(fd);
        return 0;
    }
}

/*
 * Create a new sync server list entry.
 */
sync_server_t *new_sync_server(void)
{
    sync_server_t *th = (sync_server_t *)malloc(sizeof(sync_server_t));
    if (th == NULL)
        return NULL;
    th->fd = -1;
    th->tid = 0;
    th->cat = NULL;
    th->cat_name[0] = '\0';
    th->lastcomm = time(NULL);
    th->exit = 0;
    th->prev = NULL;
    th->next = NULL;
    return th;
}

/*
 * Create a new sync client list entry.
 */
sync_client_t *new_sync_client(void)
{
    sync_client_t *th = (sync_client_t *)malloc(sizeof(sync_client_t));
    if (th == NULL)
        return NULL;
    th->fd = -1;
    th->tid = pthread_self();
    th->cat = NULL;
    th->lastcomm = time(NULL);
    th->exit = 0;
    th->prev = NULL;
    th->next = NULL;
    return th;
}

/*
 * Add a server thread to the server thread list.
 */
void add_sync_server(sync_server_t *th)
{
    if (sync_server_list.count == 0) {
        th->prev = NULL;
        th->next = NULL;
        sync_server_list.head = th;
        sync_server_list.tail = th;
    } else {
        th->next = NULL;
        th->prev = sync_server_list.tail;
        sync_server_list.tail->next = th;
        sync_server_list.tail = th;
    }
    sync_server_list.count++;
}

/*
 * Add a client thread to the client thread list.
 */
void add_sync_client(sync_client_t *th)
{
    if (sync_client_list.count == 0) {
        th->prev = NULL;
        th->next = NULL;
        sync_client_list.head = th;
        sync_client_list.tail = th;
    } else {
        th->next = NULL;
        th->prev = sync_client_list.tail;
        sync_client_list.tail->next = th;
        sync_client_list.tail = th;
    }
    sync_client_list.count++;
}

/*
 * Remove the server thread from the server thread list.
 */
void del_sync_server(sync_server_t *th)
{
    if ((sync_server_list.head == th) &&
        (sync_server_list.tail == th)) {
        sync_server_list.head = NULL;
        sync_server_list.tail = NULL;
        sync_server_list.count = 0;
    } else if (sync_server_list.head == th) {
        sync_server_list.head = th->next;
        sync_server_list.head->prev = NULL;
        sync_server_list.count--;
    } else if (sync_server_list.tail == th) {
        sync_server_list.tail = th->prev;
        sync_server_list.tail->next = NULL;
        sync_server_list.count--;
    } else {
        th->prev->next = th->next;
        th->next->prev = th->prev;
        sync_server_list.count--;
    }
}

/*
 * Remove the client thread from the client thread list.
 */
void del_sync_client(sync_client_t *th)
{
    if ((sync_client_list.head == th) &&
        (sync_client_list.tail == th)) {
        sync_client_list.head = NULL;
        sync_client_list.tail = NULL;
        sync_client_list.count = 0;
    } else if (sync_client_list.head == th) {
        sync_client_list.head = th->next;
        sync_client_list.head->prev = NULL;
        sync_client_list.count--;
    } else if (sync_client_list.tail == th) {
        sync_client_list.tail = th->prev;
        sync_client_list.tail->next = NULL;
        sync_client_list.count--;
    } else {
        th->prev->next = th->next;
        th->next->prev = th->prev;
        sync_client_list.count--;
    }
}

/*
 * Mutex lock for shared data process.
 */
void sync_lock(void)
{
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    pthread_mutex_lock(&mutex);
    mutex_tid = pthread_self();
    mutex_count++;
}

/*
 * Unlock the mutex.
 */
void sync_unlock(void)
{
    pthread_mutex_unlock(&mutex);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    mutex_tid = 0;
}

/*
 * Restart sync client. it is called from watchdog.
 */
int restart_sync_client(sync_client_t *pth)
{
    pth->exit = 0;
    pth->tid = 0;
    pth->fd = -1;
    pth->cat = NULL;
    pth->lastcomm = time(NULL);
    return pthread_create(&pth->tid, NULL, sync_client, (void *)pth);
}

/*
 * Check sync server list for thread status.
 * If the sync server is dead, release the resource.
 */
void check_sync_server(time_t now)
{
    sync_server_t *ps, *pst;

    log_msg(LOG_INFO, "watch dog: checking sync servers.");
    for (ps = sync_server_list.head; ps;) {
        pst = ps;
        ps = ps->next;
        if (pst->exit > 0) {
            log_msg(LOG_INFO, "watch dog: sync server(%u) received signal %d, cleaning", pst->tid, pst->exit);
            if (pst->cat != NULL)
                free_category(pst->cat);
            if (pst->fd != -1)
                close(pst->fd);
            del_sync_server(pst);
            free(pst);
        } else if (pst->exit < 0) {
            log_msg(LOG_INFO, "watch dog: sync server(%u) exited, cleaning",
                    pst->tid);
            if (pst->cat != NULL)
                free_category(pst->cat);
            if (pst->fd != -1)
                close(pst->fd);
            del_sync_server(pst);
            free(pst);
        } else if ((now - pst->lastcomm) > timeout) {
            log_msg(LOG_INFO,
                    "watch dog: sync server(%u) timeout, kill thread",
                    pst->tid);
            kill_thread(pst->tid);
        } else if ((maxqueuesize > 0) && (pst->cat != NULL)) {
            if (pst->cat->count > maxqueuesize) {
                log_msg(LOG_INFO, "watch dog: sync server(%u) queue too large, kill thread", pst->tid);
                kill_thread(pst->tid);
            }
        }
    }
    log_msg(LOG_INFO, "watch dog: checking sync servers ended.");
}

/*
 * Check sync client list for thread status.
 * If sync client is dead, restart client thread.
 */
void check_sync_client(time_t now)
{
    sync_client_t *pc, *pct;

    log_msg(LOG_INFO, "watch dog: checking sync clients.");
    for (pc = sync_client_list.head; pc;) {
        pct = pc;
        pc = pc->next;
        if ((pct->tid == 0) && ((now - pct->lastcomm) > 3)){
            log_msg(LOG_INFO, "watch dog: restarting sync client %s",
                    pct->cat_name);
            if (restart_sync_client(pct) != 0) {
                log_msg(LOG_ERR, "watch dog: can't restart sync client",
                        pct->cat_name);
                csync_exit(1);
            }
            continue;
        }
        if (pct->exit > 0) {
            log_msg(LOG_INFO, "watch dog: sync client(%u) received signal %d, cleaning", pct->tid, pct->exit);
            if (pct->fd != -1) {
                close(pct->fd);
                pct->fd = -1;
            }
            pct->tid = 0;
            pct->exit = 0;
        } else if (pct->exit < 0) {
            log_msg(LOG_INFO, "watch dog: sync client(%u) exited, cleaning",
                    pct->tid);
            if (pct->fd != -1) {
                close(pct->fd);
                pct->fd = -1;
            }
            pct->tid = 0;
            pct->exit = 0;
        } else if ((pct->tid != 0) && ((now - pct->lastcomm) > timeout)) {
            log_msg(LOG_INFO,
                    "watch dog: sync client(%u) timeout, kill thread",
                    pct->tid);
            kill_thread(pct->tid);
        }
    }
    log_msg(LOG_INFO, "watch dog: checking sync clients ended.");
}

/*
 * Check if we should exit.
 */
void check_csync_exit(void)
{
    sync_client_t *pc;
    sync_server_t *ps;

    if (csync_exit_thread == 0)
        return;
    log_msg(LOG_INFO, "watch dog(%u): thread %u received signal %d.",
            watchdog_server_tid, csync_exit_thread, csync_exit_status);
    log_msg(LOG_INFO, "watch dog(%u): killing all threads.",
            watchdog_server_tid);
    kill_thread(list_server_tid);
    kill_thread(sync_server_listener_tid);
    for (pc = sync_client_list.head; pc; pc = pc->next) {
        kill_thread(pc->tid);
    }
    for (ps = sync_server_list.head; ps; ps = ps->next) {
        kill_thread(ps->tid);
    }
    log_msg(LOG_INFO, "csync: exit.");
    sleep(1);
    csync_exit(csync_exit_status);
}

/*
 * Watch dog thread. It monitor the sync server and sync client threads.
 * If they are dead or timeout, the watch dog server release the resource
 * and restart the thread if needed.
 */
void *watchdog_server(void *args)
{
    time_t now;

    log_msg(LOG_INFO, "watch dog(%u): start", pthread_self());
    while (1) {
        sync_lock();
        now = time(NULL);
        check_csync_exit();
        check_sync_server(now);
        check_sync_client(now);
        sync_unlock();
        sleep(1);
    }
}

/*
 * Recover commands from archive log.
 */
time_t recover_category(sync_server_t *pth, time_t start, time_t end, char *transmod)
{
    time_t arc = (start / archinterval) * archinterval;
    command com;
    char path[256], buf[MAX_COM_LEN];
    FILE *fp;
    pthread_t tid = pthread_self();
    category *cat = pth->cat;
    struct tm tm;
    int path_len, ret;

    if (start > end)
        return start;
    update_lastcomm(&pth->lastcomm);
    snprintf(path, sizeof(path), "%s/arc_%s", archdir, pth->cat_name);
    path_len = strlen(path);
    while (arc < end) {
        if (localtime_r(&arc, &tm) == NULL)
            return 0;
        if (strftime(path + path_len, sizeof(path) - path_len, "-%Y%m%d.log",
                     &tm) == 0)
            return 0;
        fp = fopen(path, "r");
        if (fp == NULL) {
            if (errno == ENOENT) {
                arc += archinterval;
                continue;
            } else {
                log_msg(LOG_ERR, "sync server(%u): can't open file: %s", tid,
                        path);
                return 0;
            }
        }
        while (1) {
            update_lastcomm(&pth->lastcomm);
            lock_file(fileno(fp), F_RDLCK);
            if (fgets(buf, MAX_COM_LEN, fp) == NULL) {
                unlock_file(fileno(fp));
                break;
            }
            unlock_file(fileno(fp));
            if (parse_command(&com, buf) != 0) {
                log_msg(LOG_ERR, "sync server(%u): invalid command: %s",
                        tid, buf);
                continue;
            }
            if (((com.timestamp >= start) && (com.timestamp <= end))
                && (!strcmp(cat->name, com.cat))) {
                to_string(&com, buf, sizeof(buf));
                log_msg(LOG_INFO, "sync server(%u): recover command: %s",
                        tid, buf);
                ret = send_command(pth, &com, transmod);
                if (ret == -1) {
                    log_msg(LOG_ERR, "sync server(%u): can't send command: %s",
                            tid, buf);
                    log_msg(LOG_ERR, "sync server(%u): recover continue", tid);
                    continue;
                } else if (ret == -2) {
                    log_msg(LOG_ERR, "sync server(%u): can't send command: %s",
                            tid, buf);
                    log_msg(LOG_ERR, "sync server(%u): recover abort", tid);
                    fclose(fp);
                    return 0;
                }
            }
        }
        fclose(fp);
        arc += archinterval;
    }
    return end;
}

/*
 * Watch the sync list file. When there is new command appended to it,
 * this thread will read the new records to memory, parse it, and add
 * it to the command list of each category.
 */
void *list_server(void *arg)
{
    int fd, trunc = 0, ret;
    pthread_t tid;
    struct stat st;
    time_t lasttime = 0;
    time_t lastswitchtime = 0;
    off_t lastoffset = 0;
    unsigned long sn = 0;
    FILE *fp;
    char buf[MAX_COM_LEN], oldbuf[MAX_COM_LEN];
    command com;
    struct timeval tv1, tv2;
    double clock1, clock2;

    tid = pthread_self();
    pthread_detach(tid);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    log_msg(LOG_INFO, "list server(%u): start", tid);
    set_current_timestamp(read_current_timestamp());
    set_switch_timestamp(read_switch_timestamp());
    lastswitchtime = get_switch_timestamp();

    while (1) {
        if (stat(switchfile, &st) == 0) {
            if (st.st_mtime > lastswitchtime) {
                log_msg(LOG_INFO, "list server(%u): stat switch file: %s",
                    tid, switchfile);
                if (truncsize) {
                    if (st.st_size >= truncsize)
                        trunc = 1;
                }
                /*
                 * remove the file lock code. CMS group think this will
                 * slow the CMS system :(
                 * I think this is a problem caused by old syncserver
                 * processes. remove the file lock is not safe and not
                 * efficient.
                 */
                fd = open(switchfile, O_RDONLY);
                if (fd == -1) {
                    log_msg(LOG_ERR,
                            "list server(%u): can't open switch file: %s",
                            tid, switchfile);
                    goto sleep_now;
                }
                gettimeofday(&tv1, NULL);
                fp = fdopen(fd, "r");
                if (fp == NULL) {
                    log_msg(LOG_ERR,
                            "list server(%u): can't open switch file: %s",
                            tid, switchfile);
                    close(fd);
                    goto sleep_now;
                }
                if (st.st_size < lastoffset) {
                    log_msg(LOG_INFO,
                            "list server(%u): switch file truncated, rewinding",
                            tid);
                    lastoffset = 0;
                }
                if(fseek(fp, lastoffset, SEEK_SET) == -1) {
                    log_msg(LOG_ERR,
                            "list server(%u): can't seek on switch file: %s",
                            tid, switchfile);
                    close(fd);
                    goto sleep_now;
                }
                buf[0] = oldbuf[0] = '\0';
                while (fgets(buf, MAX_COM_LEN, fp) != NULL) {
                    if (!strncmp(buf, oldbuf, MAX_COM_LEN)) {
                        continue;
                    } else {
                        strncpy(oldbuf, buf, MAX_COM_LEN);
                    }
                    if (!strcasecmp(listtype, "cms")) {
                        ret = parse_cms_command(&com, buf);
                    } else if (!strcasecmp(listtype, "ftp")) {
                        ret = parse_ftp_command(&com, buf);
                    } else {
                        log_msg(LOG_ERR,
                                "list server(%u): unknown list type", tid);
                        csync_exit(1);
                    }
                    if (ret == 1) {
                        log_msg(LOG_ERR,
                                "list server(%u): invalid command: %s",
                                tid, buf);
                        continue;
                    } else if (ret == 2) {
                        log_msg(LOG_DEBUG,
                                "list server(%u): command discard: %s",
                                tid, buf);
                        continue;
                    } else {
                        if (com.timestamp < get_current_timestamp())
                            continue;
                        to_string(&com, buf, sizeof(buf));
                        log_msg(LOG_INFO, "list server(%u): %s", tid, buf);
#ifdef _DEBUG
                        log_msg(LOG_DEBUG, "list server(%u): arc command: %s",
                                tid, buf);
#endif
                        if (arc_command(&com) != 0)
                            log_msg(LOG_ERR, "list server(%u): can't archive command: %s", tid, buf);
#ifdef _DEBUG
                        log_msg(LOG_DEBUG, "list server(%u): add command: %s",
                                tid, buf);
#endif
                        sync_lock();
                        add_command(&com, 1);
                        sync_unlock();
                        set_current_timestamp(com.timestamp);
                        save_timestamp(com.cat, com.timestamp);
                    }
                }
                if (lastoffset == -1) {
                    log_msg(LOG_ERR, "list server(%u): ftell return %d",
                            tid, lastoffset);
                    lastoffset = 0;
                }
                if (truncsize && trunc) {
                    log_msg(LOG_INFO, "list server(%u): truncating switch file",
                            tid);
                    ftruncate(fd, 0);
                    lastoffset = 0;
                    trunc = 0;
                }
                gettimeofday(&tv2, NULL);
                fclose(fp);
                /*
                 * calculate the lock holding time.
                 */
                clock1 = (double)tv1.tv_sec + (double)tv1.tv_usec / 1000000;
                clock2 = (double)tv2.tv_sec + (double)tv2.tv_usec / 1000000;
                log_msg(LOG_INFO, "list server(%u): lock holding time: %lf",
                        tid, clock2 - clock1);
                lasttime = st.st_mtime;
                lastswitchtime = st.st_mtime;
                set_switch_timestamp(lastswitchtime);
                if (save_current_timestamp(get_current_timestamp()) != 0)
                    log_msg(LOG_ERR,
                            "list server(%u): can't save current timestamp",
                            tid);
                if (save_switch_timestamp(get_switch_timestamp()) != 0)
                    log_msg(LOG_ERR,
                            "list server(%u): can't save switch timestamp",
                            tid);
                lastoffset = 0;
            }
        }
        if (stat(listfile, &st) == 0) {
            if ((st.st_mtime > lasttime) || ((st.st_mtime == lasttime) && (st.st_size > lastoffset))) {
                if (truncsize) {
                    if (st.st_size >= truncsize)
                        trunc = 1;
                }
                /*
                 * remove the file lock code. CMS group think this will
                 * slow the CMS system :(
                 * I think this is a problem caused by old syncserver
                 * processes. remove the file lock is not safe and not
                 * efficient.
                 */
                fd = open(listfile, O_RDONLY);
                if (fd == -1) {
                    log_msg(LOG_ERR,
                            "list server(%u): can't open list file: %s",
                            tid, listfile);
                    goto sleep_now;
                }
                gettimeofday(&tv1, NULL);
                fp = fdopen(fd, "r");
                if (fp == NULL) {
                    log_msg(LOG_ERR,
                            "list server(%u): can't open list file: %s",
                            tid, listfile);
                    close(fd);
                    goto sleep_now;
                }
                if (st.st_size < lastoffset) {
                    log_msg(LOG_INFO,
                            "list server(%u): list file truncated, rewinding",
                            tid);
                    lastoffset = 0;
                }
                if(fseek(fp, lastoffset, SEEK_SET) == -1) {
                    log_msg(LOG_ERR,
                            "list server(%u): can't seek on list file: %s",
                            tid, listfile);
                    close(fd);
                    goto sleep_now;
                }
                buf[0] = oldbuf[0] = '\0';
                while (fgets(buf, MAX_COM_LEN, fp) != NULL) {
                    if (feof(fp)) break;
                    lastoffset = ftell(fp);
                    if (!strncmp(buf, oldbuf, MAX_COM_LEN)) {
                        continue;
                    } else {
                        strncpy(oldbuf, buf, MAX_COM_LEN);
                    }
                    if (!strcasecmp(listtype, "cms")) {
                        ret = parse_cms_command(&com, buf);
                    } else if (!strcasecmp(listtype, "ftp")) {
                        ret = parse_ftp_command(&com, buf);
                    } else {
                        log_msg(LOG_ERR,
                                "list server(%u): unknown list type", tid);
                        csync_exit(1);
                    }
                    if (ret == 1) {
                        log_msg(LOG_ERR,
                                "list server(%u): invalid command: %s",
                                tid, buf);
                        continue;
                    } else if (ret == 2) {
                        log_msg(LOG_DEBUG,
                                "list server(%u): command discard: %s",
                                tid, buf);
                        continue;
                    } else {
                        if (com.timestamp < get_current_timestamp())
                            continue;
                        to_string(&com, buf, sizeof(buf));
                        log_msg(LOG_INFO, "list server(%u): %s", tid, buf);
#ifdef _DEBUG
                        log_msg(LOG_DEBUG, "list server(%u): arc command: %s",
                                tid, buf);
#endif
                        if (arc_command(&com) != 0)
                            log_msg(LOG_ERR, "list server(%u): can't archive command: %s", tid, buf);
#ifdef _DEBUG
                        log_msg(LOG_DEBUG, "list server(%u): add command: %s",
                                tid, buf);
#endif
                        sync_lock();
                        add_command(&com, 1);
                        sync_unlock();
                        set_current_timestamp(com.timestamp);
                        save_timestamp(com.cat, com.timestamp);
                    }
                }
                if (lastoffset == -1) {
                    log_msg(LOG_ERR, "list server(%u): ftell return %d",
                            tid, lastoffset);
                    lastoffset = 0;
                }
                if (truncsize && trunc) {
                    log_msg(LOG_INFO, "list server(%u): truncating list file",
                            tid);
                    ftruncate(fd, 0);
                    lastoffset = 0;
                    trunc = 0;
                }
                gettimeofday(&tv2, NULL);
                fclose(fp);
                /*
                 * calculate the lock holding time.
                 */
                clock1 = (double)tv1.tv_sec + (double)tv1.tv_usec / 1000000;
                clock2 = (double)tv2.tv_sec + (double)tv2.tv_usec / 1000000;
                log_msg(LOG_INFO, "list server(%u): lock holding time: %lf",
                        tid, clock2 - clock1);
                lasttime = st.st_mtime;
                if (save_current_timestamp(get_current_timestamp()) != 0)
                    log_msg(LOG_ERR,
                            "list server(%u): can't save current timestamp",
                            tid);
            } else if (st.st_mtime < lasttime) {
                log_msg(LOG_ERR,
                    "list server(%u): modification timestamp rewind", tid);
            }
        } else {
            log_msg(LOG_ERR, "list server(%u): can't stat list file: %s",
                tid, listfile);
        }
    sleep_now:
        sleep(checkinterval);
    }
}

int parse_start_command(char *s, char *cat, int cat_len, time_t *t)
{
    char *p = s, *pp;

    while (*p != '\0') {
        if (!isspace(*p))
            break;
        p++;
    }
    if (*p == '\0')
        return -1;
    pp = p;
    while (*p != '\0') {
        if (isspace(*p))
            break;
        p++;
    }
    if (*p == '\0')
        return -1;
    *p = '\0';
    strncpy(cat, pp, cat_len);
    cat[cat_len - 1] = '\0';
    p++;
    while (*p != '\0') {
        if (!isspace(*p))
            break;
        p++;
    }
    pp = p;
    while (*p != '\0') {
        if (!isdigit(*p))
            break;
        p++;
    }
    *p = '\0';
    if (pp[0] == '\0')
        return -1;
    *t = strtoul(pp, NULL, 10);
    return 0;
}

/*
 * Mark the exit status when sync server exit.
 */
void sync_server_cleanup(void *arg)
{
    sync_server_t *pth = (sync_server_t *)arg;

    pth->exit = -1;
}

/*
 * Start sync server thread.
 * this thread should be run under server mode. When a sync client
 * connect sync server, the sync server whill start a server thread
 * for the client.
 */
void *sync_server(void *arg)
{
    int n, len, ret;
    int sync = 0;
    pthread_t tid;
    command *com = NULL;
    sync_server_t *pth = NULL;
    time_t clientstamp = 0, retstamp = 0, curstamp;
    const static char *helo = "sync server v1.0";
    const static char *quit = "quit";
    const static char *start = "start";
    const static char *startcsync = "csync";
    const static char *startshell = "shell";
    const static char *end = ".\n";
    char rbuf[MAX_COM_LEN], wbuf[MAX_COM_LEN], cat_name[MAX_CAT_LEN], *p;
    char logbuf[MAX_COM_LEN];

    int routinectl = 0;
    char *transm = NULL;

    pth = (sync_server_t *)arg;
    tid = pthread_self();
    pthread_detach(tid);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_cleanup_push(sync_server_cleanup, (void *)pth);
    log_msg(LOG_INFO, "sync server(%u): start", tid);
    update_lastcomm(&pth->lastcomm);
    snprintf(wbuf, sizeof(wbuf), "%s\n", helo);
    len = strlen(wbuf);
    if ((n = writen(pth->fd, wbuf, len)) != len) {
        log_msg(LOG_ERR, "sync server(%u): error writing to client", tid);
        goto quit;
    }
    while(1) {
        pthread_testcancel();
        n = readline(pth->fd, rbuf, sizeof(rbuf));
        if (n < 0) {
            log_msg(LOG_ERR,
                    "sync server(%u): error reading from client", tid);
            goto quit;
        }
        update_lastcomm(&pth->lastcomm);
        p = rbuf;
#ifdef _DEBUG
        log_msg(LOG_DEBUG, "sync server(%u): received from client: %s",
                tid, p);
#endif
        if (!strncasecmp(quit, p, strlen(quit))) {
            goto quit;
        } else if (!strncasecmp(startcsync, p, strlen(startcsync))) {
            log_msg(LOG_INFO, "sync server(%u): %s", tid, p);
            if (parse_start_command(p + strlen(startcsync), cat_name,
                                    sizeof(cat_name), &clientstamp) != 0) {
                log_msg(LOG_ERR, "sync server(%u): command parsing error",
                        tid);
                goto quit;
            }
            routinectl = 1;
            transm = "csync";
        } else if (!strncasecmp(startshell, p, strlen(startshell))) {
            log_msg(LOG_INFO, "sync server(%u): %s", tid, p);
            if (parse_start_command(p + strlen(startshell), cat_name,
                                    sizeof(cat_name), &clientstamp) != 0) {
                log_msg(LOG_ERR, "sync server(%u): command parsing error",
                        tid);
                goto quit;
            }
            routinectl = 1;
            transm = "shell";
        } else if (!strncasecmp(start, p, strlen(start))) {
            log_msg(LOG_INFO, "sync server(%u): %s", tid, p);
            if (parse_start_command(p + strlen(start), cat_name,
                                    sizeof(cat_name), &clientstamp) != 0) {
                log_msg(LOG_ERR, "sync server(%u): command parsing error",
                        tid);
                goto quit;
            }
            routinectl = 1;
            transm = "compatible";
        }
        if (routinectl) {
            if (!sync) {
                sync_lock();
                strncpy(pth->cat_name, cat_name, sizeof(cat_name));
                pth->cat_name[sizeof(cat_name)-1] = '\0';
                pth->cat = new_category(pth->cat_name);
                if (pth->cat == NULL) {
                    log_msg(LOG_ERR,
                            "sync server(%u): can't allocate new category",
                            tid);
                    sync_unlock();
                    goto quit;
                }
#ifdef _DEBUG
                log_msg(LOG_DEBUG, "sync server(%u): new category:", tid);
                print_category(pth->cat);
#endif
                sync_unlock();
                curstamp = read_timestamp(pth->cat_name);
                retstamp = clientstamp;
                while (retstamp < curstamp) {
                    pthread_testcancel();
                    retstamp = recover_category(pth, retstamp, curstamp, transm);
                    if (retstamp == 0) {
                        log_msg(LOG_ERR,
                                "sync server(%u): can't recover from %lu",
                                tid, clientstamp);
                        writen(pth->fd, end, strlen(end));
                        goto quit;
                    }
                    curstamp = read_timestamp(pth->cat_name);
                }
                update_lastcomm(&pth->lastcomm);
                sync_lock();
                add_category(pth->cat, 1);
#ifdef _DEBUG
                log_msg(LOG_DEBUG, "sync server(%u): add category:", tid);
                print_category(pth->cat);
#endif
                sync_unlock();
                curstamp = read_timestamp(pth->cat_name);
                if (retstamp < curstamp) {
                    retstamp = recover_category(pth, retstamp, curstamp, transm);
                    if (retstamp == 0) {
                        log_msg(LOG_ERR,
                                "sync server(%u): can't recover from %lu",
                                tid, clientstamp);
                        writen(pth->fd, end, strlen(end));
                        goto quit;
                    }
                }
                update_lastcomm(&pth->lastcomm);
                sync = 1;
            }
            sync_lock();
            com = get_command(pth->cat, 0);
            sync_unlock();
            while (com != NULL) {
                pthread_testcancel();
                to_string(com, logbuf, sizeof(logbuf));
                log_msg(LOG_INFO, "sync server(%u): get command: %s", tid,
                        logbuf);
                if (com->timestamp < clientstamp) {
                    free_command(com);
                    sync_lock();
                    com = get_command(pth->cat, 0);
                    sync_unlock();
                    continue;
                }
                log_msg(LOG_INFO, "sync server(%u): send command: %s", tid,
                        logbuf);
                ret = send_command(pth, com, transm);
                if (ret == 0) {
                    free_command(com);
                } else {
                    log_msg(LOG_ERR,
                            "sync server(%u): error sending command: %s", tid,
                            logbuf);
                    free_command(com);
                    if (ret == -2)
                        goto quit;
                }
                update_lastcomm(&pth->lastcomm);
                sync_lock();
                com = get_command(pth->cat, 0);
                sync_unlock();
            }
            if (writen(pth->fd, end, strlen(end)) != 2) {
                log_msg(LOG_ERR,
                        "sync server(%u): can't send ending command", tid);
                goto quit;
            }
            update_lastcomm(&pth->lastcomm);
            log_msg(LOG_INFO, "sync server(%u): sync end", tid);
        }
    }
quit:
    log_msg(LOG_INFO, "sync server(%u): quit", tid);
    sync_lock();
#ifdef _DEBUG
    log_msg(LOG_DEBUG, "sync server(%u): free category:", tid);
    print_category(pth->cat);
#endif
    if (pth->cat != NULL) {
        free_category(pth->cat);
        pth->cat = NULL;
    }
    if (pth->fd != -1) {
        close(pth->fd);
        pth->fd = -1;
    }
    pth->exit = -1;
    sync_unlock();
    pthread_cleanup_pop(0);
    return (void *)NULL;
}

/*
 * Connect to the fastest server.
 */
int connect_sync_server(server_addr addrs[], int number_servers)
{
    int i, fd;
    server_addr *addr;

    for (i = 0; i < number_servers; i++) {
        addr = &addrs[i];
        fd = client_socket(addr->addr, addr->port, pthread_self());
        if (fd == -1) {
            log_msg(LOG_ERR,
                    "sync client(%u): can't connect to server %s:%s",
                    pthread_self(), addr->addr, addr->port);
        } else {
            struct sockaddr_in client_address;
            socklen_t address_length;
            address_length = sizeof(struct sockaddr_in);
            getsockname(fd, (struct sockaddr*)&client_address, &address_length);
            log_msg(LOG_INFO, "sync client(%u): %s:%d connected to server %s:%s ok",
                    pthread_self(), inet_ntoa(client_address.sin_addr),
                    ntohs(client_address.sin_port), addr->addr, addr->port);
            return fd;
        }
    }
    return -1;
}

/*
 * Mark the exit status when sync client exit.
 */
void sync_client_cleanup(void *arg)
{
    sync_client_t *pth = (sync_client_t *)arg;

    pth->exit = -1;
}

/*
 * Start sync client thread.
 * this thread should be run in promisc mode or client mode.
 */
void *sync_client(void *arg)
{
    int fd, i, len, ret;
    pthread_t tid;
    char rbuf[MAX_COM_LEN], wbuf[MAX_COM_LEN], *p;
    const static char *start = "start %s %u\n";
    const static char *startnew = "%s %s %u\n";
    time_t last = 0;
    command com;
    sync_client_t *pth;
    char logbuf[MAX_COM_LEN];

    pth = (sync_client_t *)arg;
    tid = pthread_self();
    pthread_detach(tid);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_cleanup_push(sync_client_cleanup, (void *)pth);
    log_msg(LOG_INFO, "sync client(%u): start", tid);
    update_lastcomm(&pth->lastcomm);
    last = read_timestamp(pth->cat_name);
    if (last == 0) {
        if (startfrom == 0)
            last = time(NULL);
        else
            last = startfrom;
        log_msg(LOG_INFO,
                "sync client(%u): can't get sync timestamp, start from %u",
                tid, last);
    }
    fd = connect_sync_server(pth->addresses, pth->number_servers);
    if (fd == -1) {
        log_msg(LOG_ERR, "sync client(%u): can't connect to sync server",
                tid);
        goto quit;
    }
    update_lastcomm(&pth->lastcomm);
    sync_lock();
    pth->fd = fd;
    sync_unlock();
    if (readline(fd, rbuf, sizeof(rbuf)) > 0) {
        log_msg(LOG_INFO, "sync client(%u): %s readline ok", tid, strtrim(rbuf));
    } else {
        log_msg(LOG_INFO, "sync client(%u):  readline error", tid);
    }
    
    update_lastcomm(&pth->lastcomm);
    while (1) {
        if (!strcasecmp(pth->transfermode, "compatible")) {
        snprintf(wbuf, sizeof(wbuf), start, pth->cat_name, last);
        } else {
        snprintf(wbuf, sizeof(wbuf), startnew, pth->transfermode, pth->cat_name, last);
        }
        len = strlen(wbuf);
        if (writen(fd, wbuf, len) != len) {
            log_msg(LOG_ERR, "sync client(%u): socket writing error: %s",
                    tid, wbuf);
            goto quit;
        }
        update_lastcomm(&pth->lastcomm);
        log_msg(LOG_INFO, "sync client(%u): %s", tid, strtrim(wbuf));
        while (readline(fd, rbuf, sizeof(rbuf)) > 0) {
            pthread_testcancel();
            update_lastcomm(&pth->lastcomm);
            if (!strncmp(rbuf, ".\n", 2)) {
                log_msg(LOG_INFO, "sync client(%u): sync end", tid);
                break;
            } else {
                p = rbuf;
#ifdef _DEBUG
                log_msg(LOG_DEBUG, "sync client(%u): %s", tid, p);
#endif
                if (parse_command(&com, p) != 0) {
                    log_msg(LOG_ERR,
                            "sync client(%u): parse error", tid);
                    goto quit;
                }
                to_string(&com, logbuf, sizeof(logbuf));
                log_msg(LOG_INFO, "sync client(%u): receive command: %s",
                        tid, logbuf);
                ret = receive_command(pth, &com, pth->cat_name, pth->transfermode, pth->shellcommand, 
                            pth->shellremove, pth->shellsymbol);
                if (ret != 0) {
                    log_msg(LOG_ERR,
                            "sync client(%u): receive error: %s",
                            tid, logbuf);
                    if (ret == -2)
                        goto quit;
                }
                update_lastcomm(&pth->lastcomm);
                if (promisc) {
#ifdef _DEBUG
                    log_msg(LOG_DEBUG, "sync client(%u): archive command: %s",
                            tid, logbuf);
#endif
                    sync_lock();
                    arc_command(&com);
#ifdef _DEBUG
                    log_msg(LOG_DEBUG, "sync client(%u): add command: %s",
                            tid, logbuf);
#endif
                    add_command(&com, 1);
                    sync_unlock();
                }
                if (com.timestamp > last && ret != -2) {
                    last = com.timestamp;
                    if (save_timestamp(pth->cat_name, last) != 0)
                        log_msg(LOG_ERR,
                                "sync client(%u): can't save timestamp", tid);
                }
            }
        }
        sleep(checkinterval);
    }
quit:
    log_msg(LOG_INFO, "sync client(%u): quit", tid);
    sync_lock();
    if (pth->fd != -1) {
        close(pth->fd);
        pth->fd = -1;
    }
    pth->exit = -1;
    update_lastcomm(&pth->lastcomm);
    sync_unlock();
    pthread_cleanup_pop(0);
    return (void *)NULL;
}

/*
 * Listen on a tcp port. When a client connected, it spawn
 * a sync server thread to serve the client.
 */
void *sync_server_listener(void *arg)
{
    int sock, fd;
    char *port;
    pthread_t tid = pthread_self();

    port = (char *)arg;
    sock = server_socket(NULL, port);
    if (sock == -1) {
        log_msg(LOG_ERR, "csync(%u): can't open server socket", tid);
        csync_exit(1);
    }
    while (1) {
        fd = accept(sock, NULL, NULL);
        if (fd == -1) {
            log_msg(LOG_ERR, "csync(%u): can't accept connection", tid);
        } else {
            /* start sync server */
            sync_server_t *pth = new_sync_server();
            if (pth == NULL) {
                log_msg(LOG_ERR,
                        "csync(%u): can't allocate sync server struct", tid);
                csync_exit(1);
            }
            pth->fd = fd;
            pth->tid = 0;
            pth->lastcomm = time(NULL);
            pth->cat = NULL;
            sync_lock();
            add_sync_server(pth);
            if (pthread_create(&pth->tid, NULL, sync_server, (void *)pth)
                != 0) {
                log_msg(LOG_ERR,
                        "csync(%u): can't create sync server thread", tid);
                csync_exit(1);
            }
            struct sockaddr_in server_address;
            socklen_t address_length;
            address_length = sizeof(struct sockaddr_in);
            getpeername(pth->fd, (struct sockaddr*)&server_address, &address_length);
            log_msg(LOG_INFO, "sync server(%u): connected from client %s:%d ok",
                    pth->tid, inet_ntoa(server_address.sin_addr),
                    ntohs(server_address.sin_port));
            sync_unlock();
        }
    }
}

/*
 * Parse one server address.
 */
void parse_server_addr(server_addr *saddr, char *str)
{
    int i = 0;
    char *ip = NULL, *port = NULL;

    str = strtrim(str);
    ip = str;
    while (str[i] != '\0') {
        if (str[i] == ':') {
            str[i] = '\0';
            port = str + i + 1;
        }
        i++;
    }
    strncpy(saddr->addr, ip, MAX_ADDR_LEN);
    saddr->addr[MAX_ADDR_LEN - 1] = '\0';
    strncpy(saddr->port, port, MAX_PORT_LEN);
    saddr->port[MAX_PORT_LEN - 1] = '\0';
    saddr->priority = 0;
    saddr->disabled = 0;
}

/*
 * Parse one sync address.
 */
sync_client_t *parse_sync_addr(char *addr)
{
    sync_client_t *arg;
    char *cat = NULL, *transmod = NULL, *shellcom = NULL, *shellrem = NULL;
    char *shellsym = NULL, *sa = NULL;
    int i = 0, j = 0, k = 0;

    arg = (sync_client_t *)malloc(sizeof(sync_client_t));
    if (arg == NULL)
        return NULL;
    arg->cat_name[0] = '\0';
    arg->number_servers = 0;
    addr = strtrim(addr);
    cat = addr;
    while (addr[i] != '\0') {
        if (addr[i] == '#') {
            if (k == 0) {
                if ((i < 1) || (i >= MAX_CAT_LEN - 2)) {
                    free(arg);
                    return NULL;
                }
                addr[i] = '\0';
                strncpy(arg->cat_name, cat, MAX_CAT_LEN);
                arg->cat_name[MAX_CAT_LEN - 1] = '\0';
                transmod = addr + i + 1;
                k++;
            } else if (k == 1) {
                addr[i] = '\0';
                strncpy(arg->transfermode, transmod, MAX_TRAN_LEN);
                arg->transfermode[MAX_TRAN_LEN - 1] = '\0';
                if ((strcasecmp(arg->transfermode, "csync") != 0) &&
                    (strcasecmp(arg->transfermode, "shell") != 0) &&
                    (strcasecmp(arg->transfermode, "compatible") != 0)) {
                    strncpy(arg->transfermode, "compatible", MAX_TRAN_LEN);
                    arg->transfermode[MAX_TRAN_LEN - 1] = '\0';
                }
                shellcom = addr + i + 1;
                k++;
            } else if (k == 2) {
                addr[i] = '\0';
                strncpy(arg->shellcommand, shellcom, MAX_SHEL_LEN);
                arg->shellcommand[MAX_SHEL_LEN - 1] = '\0';
                shellrem = addr + i + 1;
                k++;
            } else if (k == 3) {
                addr[i] = '\0';
                strncpy(arg->shellremove, shellrem, MAX_SHEL_LEN);
                arg->shellremove[MAX_SHEL_LEN - 1] = '\0';
                shellsym = addr + i + 1;
                k++;
            }
        } else if (addr[i] == '@') {
            if (k == 0) {
                if ((i < 1) || (i >= MAX_CAT_LEN - 2)) {
                    free(arg);
                    return NULL;
                }
                addr[i] = '\0';
                strncpy(arg->cat_name, cat, MAX_CAT_LEN);
                arg->cat_name[MAX_CAT_LEN - 1] = '\0';
                strncpy(arg->transfermode, "compatible", MAX_TRAN_LEN);
                arg->transfermode[MAX_TRAN_LEN - 1] = '\0';
                strncpy(arg->shellcommand, "", MAX_SHEL_LEN);
                arg->shellcommand[MAX_SHEL_LEN - 1] = '\0';
                strncpy(arg->shellremove, "", MAX_SHEL_LEN);
                arg->shellremove[MAX_SHEL_LEN - 1] = '\0';
                strncpy(arg->shellsymbol, "", MAX_SHEL_LEN);
                arg->shellsymbol[MAX_SHEL_LEN - 1] = '\0';
            } else if (k == 1) {
                addr[i] = '\0';
                strncpy(arg->transfermode, transmod, MAX_TRAN_LEN);
                arg->transfermode[MAX_TRAN_LEN - 1] = '\0';
                if ((strcasecmp(arg->transfermode, "csync") != 0) &&
                    (strcasecmp(arg->transfermode, "shell") != 0) &&
                    (strcasecmp(arg->transfermode, "compatible") != 0)) {
                    strncpy(arg->transfermode, "compatible", MAX_TRAN_LEN);
                    arg->transfermode[MAX_TRAN_LEN - 1] = '\0';
                }
                strncpy(arg->shellcommand, "", MAX_SHEL_LEN);
                arg->shellcommand[MAX_SHEL_LEN - 1] = '\0';
                strncpy(arg->shellremove, "", MAX_SHEL_LEN);
                arg->shellremove[MAX_SHEL_LEN - 1] = '\0';
                strncpy(arg->shellsymbol, "", MAX_SHEL_LEN);
                arg->shellsymbol[MAX_SHEL_LEN - 1] = '\0';
            } else if (k == 2) {
                addr[i] = '\0';
                strncpy(arg->shellcommand, shellcom, MAX_SHEL_LEN);
                arg->shellcommand[MAX_SHEL_LEN - 1] = '\0';
                strncpy(arg->shellremove, "", MAX_SHEL_LEN);
                arg->shellremove[MAX_SHEL_LEN - 1] = '\0';
                strncpy(arg->shellsymbol, "", MAX_SHEL_LEN);
                arg->shellsymbol[MAX_SHEL_LEN - 1] = '\0';
            } else if (k == 3) {
                addr[i] = '\0';
                strncpy(arg->shellremove, shellrem, MAX_SHEL_LEN);
                arg->shellremove[MAX_SHEL_LEN - 1] = '\0';
                strncpy(arg->shellsymbol, "", MAX_SHEL_LEN);
                arg->shellsymbol[MAX_SHEL_LEN - 1] = '\0';
            } else if (k == 4) {
                addr[i] = '\0';
                strncpy(arg->shellsymbol, shellsym, MAX_SHEL_LEN);
                arg->shellsymbol[MAX_SHEL_LEN - 1] = '\0';
            }
        } else if (addr[i] == '[') {
            sa = addr + i + 1;
        } else if (addr[i] == '|') {
            addr[i] = '\0';
            if (j >= MAX_SERV_ADDR) {
                free(arg);
                return NULL;
            }
            parse_server_addr(&arg->addresses[j], sa);
            arg->number_servers = ++j;
            sa = addr + i + 1;
        } else if (addr[i] == ']') {
            addr[i] = '\0';
            if (j >= MAX_SERV_ADDR) {
                free(arg);
                return NULL;
            }
            parse_server_addr(&arg->addresses[j], sa);
            arg->number_servers = ++j;
            break;
        }
        i++;
    }
    return arg;
}

/*
 * Parse the server address, return an array which contains
 * pointers of struct sync_client_args.
 */
sync_client_t **parse_client_args(char *server_addr)
{
    sync_client_t **args;
    sync_client_t *arg;
    int i = 0, j = 0;
    char *p, c;

    args = (sync_client_t **)malloc(sizeof(sync_client_t *));
    if (args == NULL)
        return NULL;
    if (server_addr == NULL)
        return NULL;
    args[0] = NULL;
    p = server_addr;
    do {
        c = server_addr[i];
        if (isspace(c) || (c == ',') || (c == ';') || (c == '\0')) {
            server_addr[i] = '\0';
            arg = parse_sync_addr(p);
            p = server_addr + i + 1;
            if (arg == NULL) {
                free(args);
                return NULL;
            } else {
                args = realloc(args, sizeof(sync_client_t *) * (j + 2));
                args[j++] = arg;
                args[j] = NULL;
            }
        }
        i++;
    } while (c != '\0');
    return args;
}

#ifdef _DEBUG
void print_client_args(sync_client_t *arg)
{
    int i;

    log_msg(LOG_DEBUG, "category: %s", arg->cat_name);
    log_msg(LOG_DEBUG, "number servers: %d", arg->number_servers);
    for (i = 0; i < arg->number_servers; i++) {
        log_msg(LOG_DEBUG, "server %d: ", i + 1);
        log_msg(LOG_DEBUG, "address: %s", arg->addresses[i].addr);
        log_msg(LOG_DEBUG, "port: %s", arg->addresses[i].port);
        log_msg(LOG_DEBUG, "priority: %d", arg->addresses[i].priority);
        log_msg(LOG_DEBUG, "disabled: %d", arg->addresses[i].disabled);
    }
}
#endif

/*
 * Start sync clients.
 */
void sync_client_start(char *server_addr)
{
    pid_t pid = getpid();
    sync_client_t **args;
    int i = 0;

#ifdef _DEBUG
    log_msg(LOG_DEBUG, "%s", server_addr);
#endif
    if (server_addr == NULL)
        return;
    args = parse_client_args(server_addr);
    if (args == NULL) {
        log_msg(LOG_ERR, "csync(%u): can't parse server address", pid);
        csync_exit(1);
    }
    while (args[i] != NULL) {
#ifdef _DEBUG
        print_client_args(args[i]);
#endif
        args[i]->tid = 0;
        args[i]->lastcomm = time(NULL);
        args[i]->exit = 0;
        args[i]->fd = -1;
        sync_lock();
        add_sync_client(args[i]);
        if (pthread_create(&args[i]->tid, NULL, sync_client, args[i]) != 0) {
            log_msg(LOG_ERR, "csync(%u): can't create client thread", pid);
            csync_exit(1);
        }
        sync_unlock();
        i++;
    }
}

/*
 * Kill a thread.
 */
void kill_thread(pthread_t tid)
{
    if (tid != 0)
        pthread_cancel(tid);
}

/*
 * When receive a HUP signal, switch log file.
 */
void sig_hup(int signo)
{
    if (log_switch == 0)
        log_switch = 1;
    signal(SIGHUP, sig_hup);
}

/*
 * Signal process function
 */
void sig_quit(int signo)
{
    if (csync_exit_thread == 0) {
        csync_exit_thread = pthread_self();
        csync_exit_status = signo;
    }
    signal(signo, SIG_IGN);
}

/*
 * Init signal mask.
 */
void signal_init(void)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, sig_quit);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGALRM, SIG_IGN);
    signal(SIGINT, sig_quit);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTERM, sig_quit);
    signal(SIGILL, sig_quit);
    signal(SIGABRT, sig_quit);
    signal(SIGFPE, sig_quit);
    signal(SIGSEGV, sig_quit);
    signal(SIGBUS, sig_quit);
}

/*
 * Run as daemon.
 */
void daemon_init(void)
{
    int i;
    pid_t pid;

    if ((pid = fork()) != 0)
        exit(0);
    setsid();
    if ((pid = fork()) != 0)
        exit(0);
    chdir("/");
    umask(0);
}

/*
 * close log, remove pidfile and exit.
 */
void csync_exit(int status)
{
    log_close();
    unlink(option_get_str("pidfile"));
    exit(status);
}

/*
 * Save pid to pidfile.
 */
void save_pid(void)
{
    char *path, buf[32];
    int fd;

    path = option_get_str("pidfile");
    snprintf(buf, sizeof(buf), "%u", getpid());
    fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1) {
        log_msg(LOG_ERR, "csync(%s): can't open pidfile: %s", buf, path);
        csync_exit(1);
    }
    write(fd, buf, strlen(buf));
    close(fd);
}

/*
 * Fetch the uid of a user
 */
uid_t uidof(char *uname)
{
    struct passwd *pwd;

    pwd = getpwnam(uname);
    if (pwd == NULL) {
        log_msg(LOG_ERR, "no such user: %s, exit", uname);
        csync_exit(1);
    }
    return pwd->pw_uid;
}

/*
 * Fetch the gid of a group
 */
gid_t gidof(char *gname)
{
    struct group *grp;

    grp = getgrnam(gname);
    if (grp == NULL) {
        log_msg(LOG_ERR, "no such group: %s, exit", gname);
        csync_exit(1);
    }
    return grp->gr_gid;
}

void init_uid(void)
{
    uid_t uid;
    gid_t gid;
    char *id;

    id = option_get_str("userid");
    if (isdigit(id[0]))
        uid = atoi(id);
    else
        uid = uidof(id);
    id = option_get_str("groupid");
    if (isdigit(id[0]))
        gid = atoi(id);
    else
        gid = gidof(id);
    /* drop privileges */
    setregid(gid, gid);
    setreuid(uid, uid);
}

/*
 * The main function.
 */
int main(int argc, char **argv)
{
    int sock, fd;
    pthread_t tid;
    unsigned int old_mutex_count = mutex_count;
    unsigned int n;

    if ((argc = 2) && ((!strcasecmp(argv[1], "-version")) ||
                       (!strcasecmp(argv[1], "-v")))) {
        printf("%s\n", version);
        exit(0);
    }

    umask(022);
    if (parse_options(argc, argv))
        exit(1);
    init_uid();
    if (option_get_bool("daemon"))
        daemon_init();
    signal_init();
    log_open();
    save_pid();
    log_msg(LOG_INFO, "csync(%u): start", getpid());
    maxqueuesize = option_get_int("maxqueuesize");
    catdirdepth = option_get_int("catdirdepth");
    truncsize = option_get_int("truncsize");
    syncroot = option_get_str("syncroot");
    rsyncroot = option_get_str("rsyncroot");
    listtype = option_get_str("listtype");
    listfile = option_get_str("listfile");
    switchfile = option_get_str("switchfile");
    archdir = option_get_str("archdir");
    checkinterval = option_get_int("listcheckinterval");
    archinterval = option_get_int("archinterval");
    timeout = option_get_int("timeout");
    preservetimes = option_get_str("preservetimes");
    savefiledir = option_get_str("savefiledir");
    mode = option_get_str("mode");
    startfrom = option_get_ulong("startfrom");
    categories = ght_create(CATS_HASH_SIZE);
    if (!strcasecmp(mode, "server")) { /* server mode */
        /* start watch dog */
        if (option_get_bool("watchdog"))
            pthread_create(&watchdog_server_tid, NULL, watchdog_server,
                           (void *)categories);
        /* start list server */
        pthread_create(&list_server_tid, NULL, list_server, NULL);
        /* waiting for connection of sync client */
        pthread_create(&sync_server_listener_tid, NULL, sync_server_listener,
                       (void *)option_get_str("bindport"));
    } else if (!strcasecmp(mode, "client")) { /* client mode */
        /* start watch dog */
        if (option_get_bool("watchdog"))
            pthread_create(&watchdog_server_tid, NULL, watchdog_server,
                           (void *)categories);
        /* start sync clients */
        sync_client_start(option_get_str("serveraddress"));
    } else if (!strcasecmp(mode, "promisc")) { /* promisc mode */
        promisc = 1;
        /* start watch dog */
        if (option_get_bool("watchdog"))
            pthread_create(&watchdog_server_tid, NULL, watchdog_server,
                           (void *)categories);
        /* waiting for connection of sync client */
        pthread_create(&sync_server_listener_tid, NULL, sync_server_listener,
                       (void *)option_get_str("bindport"));
        /* start sync clients */
        sync_client_start(option_get_str("serveraddress"));
    } else {
        log_msg(LOG_ERR, "unknown run mode");
        csync_exit(1);
    }
    while (1) { /* detect dead lock */
        n = 10;
        while ((n = sleep(n)) != 0);
        if (mutex_count != old_mutex_count) {
            old_mutex_count = mutex_count;
        } else {
            tid = mutex_tid;
            if (tid != 0) {
                if (tid == watchdog_server_tid) {
                    log_msg(LOG_ERR, "watchdog(%u) got dead lock?",
                            tid);
                } else if (tid == list_server_tid) {
                    log_msg(LOG_ERR, "listserver(%u) got dead lock?",
                            tid);
                } else if (tid == sync_server_listener_tid) {
                    log_msg(LOG_ERR, "listener(%u) got dead lock?", tid);
                } else {
                    log_msg(LOG_ERR, "thread(%u) got dead lock?", tid);
                }
            }
        }
    }
}