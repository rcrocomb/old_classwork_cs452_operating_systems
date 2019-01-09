#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include "utility.h"
#include "handler.h"

extern int debugflag2;
extern int device_mbox_ID[];

/* an error method to handle invalid syscalls */

void
nullsys(sysargs *args)
{
    KERNEL_ERROR("Invalid syscall");
}

/*!
    time_slice() makes the decisions as to whether to call the
    dispatcher or not.

    I'm not sure if any return values from MboxCondSend() are errors
    when called in this context, so I'm ignoring the return code.
*/

void
clock_handler(int dev, int unit)
{
    static int counts = 0;
    DP2(DEBUG3,"handler called\n");

    if (dev != CLOCK_DEV)
        KERNEL_ERROR("non-clock device calling clock's handler: %d", dev);

    if (counts % 5 == 0)
    {
        (void)MboxCondSend(device_mbox_ID[CLOCK_DEV], NULL, 0);
        counts = 0;
    }

    ++counts;
    time_slice();
}


/*!
    Ignoring return value of conditional send: see clock_handler comments.
*/

void
disk_handler(int dev, int unit)
{
    int status_reg;
    DP2(DEBUG3,"disk_handler(): handler called\n");

    if (dev != DISK_DEV)
        KERNEL_ERROR("non-disk device calling disks' handler: %d", dev);

    if ((unit < 0) || (unit > DISK_UNITS))
        KERNEL_ERROR("Invalid unit %d for disk device in handler", unit);

    device_input(DISK_DEV, unit, &status_reg);

    (void)MboxCondSend(device_mbox_ID[DISK_DEV + unit],
                       &status_reg,
                       sizeof(status_reg));
}

/*!
    Ignoring return value of conditional send: see clock_handler comments.
*/

void
term_handler(int dev, int unit)
{
    int status_reg;
    DP2(DEBUG3,"term_handler(): handler called\n");

    if (dev != TERM_DEV)
        KERNEL_ERROR("non-terminal device calling terminals' handler: %d", dev);

    if ((unit < 0) || (unit > TERM_UNITS))
        KERNEL_ERROR("Invalid unit %d for terminal device in handler", unit);

    device_input(TERM_DEV, unit, &status_reg);
    DP2(DEBUG3, "Status value == %02x (%d)\n", status_reg, status_reg);

    (void)MboxCondSend(device_mbox_ID[TERM_DEV + unit],
                       &status_reg,
                       sizeof(status_reg));
}

void
syscall_handler(int dev, int unit)
{
    DP2(DEBUG3, "syscall_handler(): handler called\n");
}

