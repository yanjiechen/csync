/*
 * Copyright (c) SOHU INC.
 * Author: Chen Yanjie <yanjiechen@sohu-inc.com>
 */

#ifndef _LOG_H
#define _LOG_H

void log_open(void);
void log_close(void);
void log_msg(int, const char *, ...);

#endif