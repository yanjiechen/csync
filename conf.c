/*
 * Copyright (c) SOHU INC.
 * Author: Chen Yanjie <yanjiechen@sohu-inc.com>
 *
 * This file include functions for config file parsing, command line
 * option parsing, and option fetching.
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <limits.h>
#include <stdlib.h>
#include "conf.h"
#include "log.h"
#include "str.h"

static option options[] = {
    /*  option name  | value |  default value | type */
    {"mode",              NULL, "client",               TYPE_STR},
    {"daemon",            NULL, "true",                 TYPE_BOOL},
    {"archdir",           NULL, NULL,                   TYPE_STR},
    {"bindaddress",       NULL, NULL,                   TYPE_STR},
    {"bindport",          NULL, "65530",                TYPE_STR},
    {"serveraddress",     NULL, NULL,                   TYPE_STR},
    {"serverport",        NULL, "65530",                TYPE_INT},
    {"timeout",           NULL, "60",                   TYPE_INT},
    {"watchdog",          NULL, "true",                 TYPE_BOOL},
    {"preservetimes",     NULL, "no",                   TYPE_STR},
    {"category",          NULL, NULL,                   TYPE_STR},
    {"listcheckinterval", NULL, "3",                    TYPE_INT},
    {"archinterval",      NULL, "86400",                TYPE_INT},
    {"logfile",           NULL, "/var/log/csync.log",   TYPE_STR},
    {"listtype",          NULL, "cms",                  TYPE_STR},
    {"listfile",          NULL, "/opt/sync_list.dat",   TYPE_STR},
    {"switchfile",        NULL, "/opt/sync_switch.dat", TYPE_STR},
    {"savefiledir",       NULL, "/var/csync",           TYPE_STR},
    {"syncroot",          NULL, "/opt",                 TYPE_STR},
    {"rsyncroot",         NULL, NULL,                   TYPE_STR},
    {"truncsize",         NULL, "0",                    TYPE_INT},
    {"maxqueuesize",      NULL, "0",                    TYPE_INT},
    {"catdirdepth",       NULL, "2",                    TYPE_INT},
    {"logbufsize",        NULL, "65536",                TYPE_INT},
    {"pidfile",           NULL, "/var/run/csync.pid",   TYPE_STR},
    {"userid",            NULL, "root",                 TYPE_STR},
    {"groupid",           NULL, "root",                 TYPE_STR},
    {"startfrom",         NULL, "0",                    TYPE_STR},
    {NULL,                NULL, NULL,                   0}
};

/*
 * Checking if an option is null
 */
int option_is_null(char *name)
{
    int i = 0;

    while (options[i].name != NULL) {
        if (!strcmp(name, options[i].name)) {
            if ((options[i].value == NULL) && (options[i].defvalue == NULL))
                return 1;
            else
                return 0;
        } else {
            ++i;
        }
    }
}

/*
 * Get an integer option
 */
int option_get_int(char *name)
{
    int i = 0;
    char *value;

    while (options[i].name != NULL) {
        if (!strcmp(name, options[i].name)) {
            if (options[i].value == NULL)
                value = options[i].defvalue;
            else
                value = options[i].value;
            return atoi(value);
        } else {
            ++i;
        }
    }
    return 0;
}

/*
 * Get an unsigned long option
 */
unsigned long option_get_ulong(char *name)
{
    int i = 0;
    char *value, *p;
    unsigned long ul;

    while (options[i].name != NULL) {
        if (!strcmp(name, options[i].name)) {
            if (options[i].value == NULL)
                value = options[i].defvalue;
            else
                value = options[i].value;
            ul = strtoul(value, &p, 10);
            if ((*p != 0) || (ul == ULONG_MAX))
                return 0;
            else
                return ul;
        } else {
            ++i;
        }
    }
    return 0;
}

/*
 * Get an string option
 */
char *option_get_str(char *name)
{
    int i = 0;

    while (options[i].name != NULL) {
        if (!strcmp(name, options[i].name)) {
            if (options[i].value == NULL)
                return options[i].defvalue;
            else
                return options[i].value;
        } else {
            ++i;
        }
    }
    return NULL;
}

/*
 * Get an bool option
 */
int option_get_bool(char *name)
{
    int i = 0;
    char *value;

    while (options[i].name != NULL) {
        if (!strcmp(name, options[i].name)) {
            if (options[i].value == NULL)
                value = options[i].defvalue;
            else
                value = options[i].value;
            if (!strcasecmp("on", value) || !strcasecmp("true", value)
                || !strcasecmp("yes", value))
                return 1;
            else if (!strcasecmp("off", value) || !strcasecmp("false", value)
                || !strcasecmp("no", value))
                return 0;
            else {
                log_msg(LOG_ERR, "invalid option: %s = %s", name, value);
                return 0;
            }
        } else {
            ++i;
        }
    }
    return 0;
}

/*
 * Parse one config line.
 */
static int parse_one_line(char *line, char *key, char *val)
{
    char *p, *pp;

    pp = strtrim(line);
    p = strchr(pp, '=');
    if (p != NULL) {
        *p = '\0';
        strncpy(key, strtrim(pp), MAX_LINE_LEN);
        strncpy(val, strtrim(++p), MAX_LINE_LEN);
        return 0;
    } else if(pp[0] == '\0')
        return 1;
    else
        return -1;
}

/*
 * Parse config file.
 */
int parse_conf_file(char *path)
{
    char key[MAX_LINE_LEN], val[MAX_LINE_LEN], buf[MAX_LINE_LEN];
    char line[MAX_LINE_LEN];
    char *p;
    int len, i;
    FILE *fp;

    if ((fp = fopen(path, "r")) == NULL) {
        return -1;
    }
    line[0] = '\0';
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        if (strlen(buf) >= MAX_LINE_LEN - 1) {
            log_msg(LOG_ERR, "parse error: line too long");
            goto error_exit;
        }
        p = strtrim(buf);
        if (p[0] == '#')    /* comments, continue */
            continue;
        len = strlen(p) - 1;
        if (p[len] == '\\') {    /* line continue */
            p[len] = '\0';
            len = MAX_LINE_LEN - strlen(line);
            strncat(line, p, len);
            if (strlen(line) >= MAX_LINE_LEN - 1) {
                log_msg(LOG_ERR, "parse error: %s line too long", path);
                goto error_exit;
            }
            continue;
        }
        len = MAX_LINE_LEN - strlen(line);
        strncat(line, p, len);
        if (strlen(line) >= MAX_LINE_LEN - 1) {
            log_msg(LOG_ERR, "parse error: %s line too long",
                path);
            goto error_exit;
        }
        if (parse_one_line(line, key, val) == -1) {
            log_msg(LOG_ERR, "syntax error: %s: %s", path, buf);
            goto error_exit;
        }
        i = 0;
        while (options[i].name != NULL) {
            if (options[i].value != NULL) {
                ++i;
                continue;
            }
            if (!strcasecmp(options[i].name, key)) {
                if (val[0] != '\0')
                    options[i].value = stralloc(val);
                break;
            } else {
                ++i;
            }
        }
        line[0] = '\0';
    } /* end while(fgets()) */
    fclose(fp);
    return 0;
error_exit:
    fclose(fp);
    return -2;
}

/*
 * Parse arguments.
 */
int parse_options(int argc, char **argv)
{
    int i = 0, j = 0, ret;
    char *conf_file = NULL;

    for (i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (!strcasecmp("-help", argv[i])) {
                print_help(argv[0]);
                exit(0);
            } else if (!strcasecmp("-config", argv[i])) {
                i++;
                conf_file = argv[i];
            } else {
                j = 0;
                while (options[j].name != NULL) {
                    if (!strcasecmp(options[j].name, argv[i] + 1)) {
                        ++i;
                        options[j].value = stralloc(argv[i]);
                        break;
                    } else {
                        ++j;
                    }
                }
                if (options[j].name == NULL) {
                    log_msg(LOG_ERR, "unknown option: %s", argv[i]);
                    return -1;
                }
            } /* end if(!strcasecmp()) */
        } else {
            print_help(argv[0]);
            exit(0);
        } /* end if(argv) */
    } /* end for () */
    if (conf_file != NULL) {
        ret = parse_conf_file(conf_file);
        if (ret) {
            log_msg(LOG_ERR, "can't read config file: %s", conf_file);
            return -1;
        }
    } else {
        if (parse_conf_file(DEFAULT_CONF_FILE) == -1)
            parse_conf_file("csync.conf");
    }
    return 0;
}

/*
 * Print help message.
 */
void print_help(char *prog)
{
    printf("Usage: %s [option]\n\n", prog);
    printf("Options:\n");
    printf(" -help                         help message\n");
    printf(" -config filename              config file path\n");
    printf(" -mode [server|client|promisc] run mode\n");
    printf(" -category category            sync category\n");
    printf(" -daemon [true|false]          run as daemon\n");
    printf(" -logfile filename             log file path\n");
    printf(" -bindaddress address          listen on this address\n");
    printf(" -bindport port                listen on this port\n");
    printf(" -serveraddress address        server address\n");
    printf(" -serverport port              server port\n");
    printf(" -timeout n                    communication timeout(sec)\n");
    printf(" -preservetimes [yes|no]       preserve file times\n");
    printf(" -listfile filename            path of listfile\n");
    printf(" -switchfile filename          path of switchfile\n");
    printf(" -listcheckinterval n          check listfile every n sec\n");
    printf(" -archinterval n               switch arch file every n sec\n");
    printf(" -savefiledir path             arch saving directory\n");
    printf(" -syncroot path                sync root(default /opt)\n");
    printf(" -maxqueuesize n               max queue size of sync server\n");
    printf(" -truncsize n                  truncate listfile when it's larger than n(bytes)\n");
}

/*
 * Print config for debug
 */
void print_conf(void)
{
    int i = 0;

    printf("name\t\tvalue\t\tdefault value\n");
    while (options[i].name != NULL) {
        printf("%s\t\t%s\t\t%s\n", options[i].name, options[i].value,
            options[i].defvalue);
        ++i;
    }
}
