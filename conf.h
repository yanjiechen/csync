/*
 * Copyright (c) SOHU INC.
 * Author: Chen Yanjie <yanjiechen@sohu-inc.com>
 */

#ifndef _CONF_H
#define _CONF_H

extern struct conf config;

#define TYPE_INT  0
#define TYPE_STR  1
#define TYPE_BOOL 2

#define MAX_LINE_LEN    16384    /* the max length of array which is readed from the conf file */
#define DEFAULT_CONF_FILE    "/usr/local/etc/csync.conf"    /* the default conf file if you type nothing after the '-config' */

struct option {
    char *name;     /* option name   */
    char *value;    /* option value  */
    char *defvalue; /* default value */
    int type;       /* option type   */
};
typedef struct option option;

/* error in as5 */

int option_is_null(char *);
int option_get_int(char *);
char *option_get_str(char *);
int option_get_bool(char *);
char *trim(char *);
void print_help(char *);
void print_conf(void);
int parse_conf_file(char *);
int parse_options(int, char **);

#endif