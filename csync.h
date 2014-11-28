/*
 * Copyright (c) SOHU INC.
 * Author: Chen Yanjie <yanjiechen@sohu-inc.com>
 */

#ifndef _CSYNC_H
#define _CSYNC_H

#define MAX_CAT_LEN    64
#define MAX_COM_LEN    256
#define CATS_HASH_SIZE 128
#define MAX_SERV_ADDR  3
#define MAX_ADDR_LEN   64
#define MAX_PORT_LEN   6
#define MAX_TRAN_LEN   64
#define MAX_SHEL_LEN   256

/*
 * The command structure. It's read and parsed from a line of synclist file.
 */
struct command {
    time_t timestamp; /* command timestamp */
    char op; /* operator */
    char cat[MAX_CAT_LEN]; /* the category */
    char arg0[MAX_COM_LEN]; /* first argument */
    char arg1[MAX_COM_LEN]; /* second argument, maybe empty */
    size_t size; /* file length */
    struct command *next; /* next command */
};

typedef struct command command;

/*
 * The category structure.
 */
struct category {
    char name[MAX_CAT_LEN]; /* category name */
    pthread_t tid; /* thread id attached with this category */
    unsigned int count; /* number of command */
    struct command *head; /* command list head */
    struct command *tail; /* command list tail */
    struct category *prev; /* previous category structure */
    struct category *next; /* next category structure */
};

typedef struct category category;

/*
 * list server's arguments structure.
 */
typedef struct {
    char *listfile; /* the list file to open. */
    char *switchfile; /* the switch file to open. */
    char *archdir; /* the archived list file directory. */
    int checkinterval; /* the check interval of synclist file */
    int archinterval; /* the archive interval of synclist file */
    ght_hash_table_t *cats; /* the category hash structure */
} list_server_args;

/*
 * sync server's arguments structure
 */
typedef struct {
    int fd; /* socket fd */
    struct sockaddr_in clientaddr; /* client address and port */
    struct category *cat; /* the category that sync server serves for */
} sync_server_args;

/*
 * server address structure
 */
typedef struct {
    char addr[MAX_ADDR_LEN];
    char port[MAX_PORT_LEN];
    int priority;
    int disabled;
} server_addr;

/*
 * sync client's arguments structure
 */
typedef struct {
    char category[MAX_CAT_LEN]; /* category name */
    char root[MAX_CAT_LEN]; /* the root of local directory to sync */
    server_addr addresses[MAX_SERV_ADDR]; /* sync server's ip and port */
    int number_servers; /* number sync servers in serv_addr */
} sync_client_args;

/*
 * the sync server thread linked list structure. used by watchdog server to
 * check the status of sync server.
 */
struct sync_server_struct {
    int fd;
    pthread_t tid;
    time_t lastcomm; /* timestamp of last communication */
    int exit;
    char cat_name[MAX_CAT_LEN];
    category *cat;
    struct sync_server_struct *prev;
    struct sync_server_struct *next;
};
typedef struct sync_server_struct sync_server_t;

/*
 * the sync client thread linked list structure. used by watchdog server to
 * check the status of sync client.
 */
struct sync_client_struct {
    int fd;
    pthread_t tid;
    time_t lastcomm; /* timestamp of last communication */
    int exit;
    char cat_name[MAX_CAT_LEN];
    char transfermode[MAX_TRAN_LEN];
    char shellcommand[MAX_SHEL_LEN];
    char shellremove[MAX_SHEL_LEN];
    char shellsymbol[MAX_SHEL_LEN];
    category *cat;
    server_addr addresses[MAX_SERV_ADDR]; /* sync server's ip and port */
    int number_servers; /* number sync servers in serv_addr */
    struct sync_client_struct *prev;
    struct sync_client_struct *next;
};
typedef struct sync_client_struct sync_client_t;

/*
 * the list header structure.
 */
struct sync_server_list_struct {
    sync_server_t *head;
    sync_server_t *tail;
    unsigned int count;
};
typedef struct sync_server_list_struct sync_server_list_t;

struct sync_client_list_struct {
    sync_client_t *head;
    sync_client_t *tail;
    unsigned int count;
};
typedef struct sync_client_list_struct sync_client_list_t;

void daemon_init(void);
void sig_quit(int);
void sig_term(int);
void sig_hup(int);
void kill_thread(pthread_t);
void *list_server(void *);
void *sync_server(void *);
void *sync_client(void *);
void tcp_server(int);
void csync_exit(int);

#endif