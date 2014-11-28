#!/bin/sh
#
# Csync Watch Dog
# check every 3 minutes
# */3 * * * * /opt/itc/csync/bin/csyncwatchdog.sh >/dev/null 2>/dev/null
#
# 2005-04-18 12:23
# 2005-04-15 14:26
# 2005.01.11 18:36
# Zhang Xiuling <xiulingzhang@sohu-inc.com>
#

PATH="/sbin:/usr/sbin:/bin:/usr/bin:$PATH"
export PATH

if ! ps axf| grep "csync .*-config" | grep -v grep >/dev/null 2>/dev/null; then
    /sbin/service csync stop
    killall csync
    sleep 2

    # retry 3 times to ensure csync run
    RunningFlag=0
    COUNT=1
    while [ $RunningFlag -ne 1 -a $COUNT -lt 4 ]
    do
        /sbin/service csync start
        sleep 2
        RunningFlag=$(ps axf| grep -v grep | grep -ic "csync .*-config")
        #echo $RunningFlag
        #let "COUNT += 1"
        COUNT=`expr $COUNT + 1`
    done
fi