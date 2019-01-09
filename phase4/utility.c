#include "utility.h"

/*!
    Author: Robert Crocombe
    Class: CS452 Operating Systems Spring 2005
    Initial Release: Fri Feb  4 14:31:55 MST 2005
    Target: gcc 3.4+ | Solaris

    gcc-specific (or maybe C-99 specific) variadic macros abound.  I
    certainly use __func__, which isn't in C89.
*/

#include <stdlib.h>
#include <string.h>         /* strlen, strncat, strcpy */
#include <stdarg.h>         /* variadic stuff, see smoosh() routine */


/*!
    Sets Processor Status Register (PSR) Current interrupt mode bit (bit 1)
    to 1, i.e. enables USELESS interrupts.
*/

void
enableInterrupts(void)
{
    DP(DEBUG4, "Enabling interrupts\n");
    KERNEL_MODE_CHECK;
    psr_set(psr_get() | PSR_CURRENT_INT);
}

/*
    Disables interrupts by setting the Process Status Register (PSR)
    Current interrupt mode bit (bit 1) to 0.

    See restoreInterrupts() for some of my difficulties with these.
*/

void
disableInterrupts(void)
{
    DP(DEBUG4, "Disabling interrupts\n");
    KERNEL_MODE_CHECK;
    psr_set(psr_get() & ~PSR_CURRENT_INT);
}


/*!

*/

void
bad_interrupt(int dev, int unit)
{
    KERNEL_ERROR("called for device %d unit %d", dev, unit);
}

/*!
    Sets bits within PSR to change this task from kernel mode (must be
    in kernel mode to call this function) to user mode.  This is done
    by clearing bit 0 of the PSR.
*/

void
go_user_mode(void)
{
    psr_set(psr_get() & ~PSR_CURRENT_MODE);
}


/*!
    Adds all kinds of goodies about kernel errors before calling
    console and halt: function from which this was called, the
    filename, and the line in that file.
*/

void
kernel_error(const char *func, const int line, const char *file, const char *message, const int do_halt)
{
    console("%s(): on line %d in %s, error is:\n%s\nHalting!\n",
            func, line, file, message);
    if (do_halt)
        halt(1);
}

void
kernel_warning
(
    const char *func,
    const int line,
    const char *file,
    const char *message
)
{
    console("%s(): on line %d in %s:\n%s\n", func, line, file, message);
}

/*!
    I heart variadic macros (and how gcc properly handles them).  I
    use smoosh() to smoosh together lots of printf-like formatting
    information into a single char * that can be used for other
    things: mostly passed to other routines.

    Note that smoosh's buffer is static (so I can point to it), so not
    threadsafe.
*/

char *
smoosh(const char * format, ...)
{
    // not good practice, but WTF, yo.  Can't overflow because of
    // vsnprintf(), but could truncate.
    #define BUF_SIZE (512+1)
    static char buffer[BUF_SIZE];

    va_list ap;

    va_start( ap, format);
    vsnprintf(buffer, BUF_SIZE, format, ap);
    va_end(ap);

    return buffer;
}

/*!
    Set the interrupt states of terminal 'unit' as defined by 'flags'.
    This is a combination of:

    RX_ON       enable interrupts that occur when a character is received
    RX_OFF      disable interrupts that occur when a character is received
    TX_ON       enable interrupts that occur when a character is transmitted
    TX_OFF      disable interrupts that occur when a character is transmitted

    Four bits are required, because there are really additional
    states: don't do anything to the Rx or Tx bits.  For instance, if
    'flags' is simply TX_ON, that indicates nothing about the Rx bit, so
    that is not altered.
*/

void
set_term_interrupts(int unit, int flags)
{
    int status;
    int ret = device_input(TERM_DEV, unit, &status);
    if (ret)
        KERNEL_ERROR("retrieving status info for terminal %d", unit);

    /* If not specifically asked to turn on or off, leave it alone */
    if (flags | RX_ON)
        status |= TERM_RX_INT;
    else if (flags | RX_OFF)
        status &= ~TERM_RX_INT;

    /* If not specifically asked to turn on or off, leave it alone */
    if (flags | TX_ON)
        status |= TERM_TX_INT;
    else if (flags | TX_OFF)
        status &= ~TERM_TX_INT;

    ret = device_output(TERM_DEV, unit, (void *)status);
    if (ret)
        KERNEL_ERROR("writing control data for terminal %d", unit);
}

