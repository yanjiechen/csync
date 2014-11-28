/*
 * Copyright (c) SOHU INC.
 * Author: Chen Yanjie <yanjiechen@sohu-inc.com>
 */

#ifndef _LOCK_H
#define _LOCK_H

void lock_file(int, short mode);
void unlock_file(int);

#endif