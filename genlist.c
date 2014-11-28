#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "lock.h"

main(int argc, char **argv)
{
    int fd;
    FILE *fp;
    char rbuf[256];
    char wbuf[256];
    int i = 1, count = 10;

    if (argc != 3)
        return -1;
    fp = fopen(argv[1], "r");
    if (fp == NULL) {
        printf("can't open %s\n", argv[1]);
        return -1;
    }
    while (1) {
        fd = open(argv[2], O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (fd == -1) {
            printf("can't open %s\n", argv[2]);
            fclose(fp);
            return -1;
        }
        lock_file(fd, F_WRLCK);
        for (i = 0; i < count; ++i) {
            if (fgets(rbuf, sizeof(rbuf), fp) == NULL) {
                fclose(fp);
                close(fd);
                unlock_file(fd);
                return 0;
            } else {
                snprintf(wbuf, sizeof(wbuf), "%lu + %s", time(NULL), rbuf);
                write(fd, wbuf, strlen(wbuf));
            }
        }
        unlock_file(fd);
        close(fd);
        sleep(1);
    }
    fclose(fp);
    return 0;
}