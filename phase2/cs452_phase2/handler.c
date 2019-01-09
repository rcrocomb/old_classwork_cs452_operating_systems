#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include "utility.h"
#include "handler.h"

extern int debugflag2;

/* an error method to handle invalid syscalls */

void
nullsys(sysargs *args)
{
    KERNEL_ERROR("Invalid syscall");
}

/*!
    time_slice() makes the decisions as to whether to call the
    dispatcher or not.
*/

void
clock_handler(int dev, int unit)
{
    DP2(DEBUG,"handler called\n");
    time_slice();
}

void
disk_handler(int dev, int unit)
{
    DP2(DEBUG,"disk_handler(): handler called\n");
}

void
term_handler(int dev, int unit)
{
    DP2(DEBUG,"term_handler(): handler called\n");
}

void
syscall_handler(int dev, int unit)
{
    DP2(DEBUG, "syscall_handler(): handler called\n");
}

