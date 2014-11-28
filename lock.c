/*
 * Copyright (c) SOHU INC.
 * Author: Chen Yanjie <yanjiechen@sohu-inc.com>
 *
 * This file include file lock function.
 */

#include <unistd.h>
#include <fcntl.h>

void lock_file(int fd, short mode)
{
    struct flock lock;
    lock.l_type = mode;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    fcntl(fd, F_SETLKW, &lock);
}

void unlock_file(int fd)
{
    struct flock lock;
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    fcntl(fd, F_SETLK, &lock);
}