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
    DP2(DEBUG4, "Enabling interrupts\n");
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
    DP2(DEBUG4, "Disabling interrupts\n");
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

int
count_links(mailbox *box)
{
    proc_entry *p;

    if (!box)
        KERNEL_ERROR("Null mailbox");

    if (!box->front)
    {
        DP2(DEBUG, "Queue is empty\n");
        return 0;
    }

    p = box->front;
    int count = 0; 
    
    if (p)
    {
        do {
            ++count;
            p = p->next;
            if (count > 10)
            {
                DP2(DEBUG, "Gone infinite: count == %d\n", count);
                break;
            }
        } while (p);
    }
    return count;
}
