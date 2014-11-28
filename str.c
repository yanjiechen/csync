/*
 * Copyright (c) SOHU INC.
 * Author: Chen Yanjie <yanjiechen@sohu-inc.com>
 *
 * This file include string processing functions.
 */
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "str.h"
#include "log.h"

/*
 * Trim the white space at the begining and end of a string.
 * Return: a string with white space at the beginning and end trimmed.
 */
char *strtrim(char *str)
{
    int len = strlen(str);
    char *p = str + len;

    while (p != str) {    /* trim the white space at the end of string */
        --p;
        if (isspace(*p))
            *p = '\0';
        else
            break;
    }

    p = str;
    while (*p != '\0') {    /* trim the beginning white space */
        if (isspace(*p))
            ++p;
        else
            break;
    }
    return p;
}

/*
 * Replace char in a string.
 */
void strreplace(char *str, char old, char new)
{
    char *p = str;

    while (*p != '\0') {
        if (*p == old)
            *p = new;
        ++p;
    }
}

/*
 * Alloc memory for a string.
 */
char *stralloc(char *str)
{
    int len = strlen(str);
    char *ret = (char *)calloc(len + 1, 1);
    if (ret == NULL) {
        log_msg(LOG_ERR, "can't alloc memory for string");
    } else {
        strncpy(ret, str, len + 1);
    }
    return ret;
}

/*
 * Free memory for a string.
 */
void strfree(char *str)
{
    free((void *)str);
}