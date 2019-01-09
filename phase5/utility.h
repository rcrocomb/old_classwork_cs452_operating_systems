#ifndef UTILITY_H
#define UTILITY_H

/******************************************************************************/
/* Author: Robert Crocombe                                                    */
/* Class: CS452 Spring 05                                                     */
/* Assignment: Phase 3                                                        */
/*                                                                            */
/* I tried to put all of the more general purpose items in this header and    */
/* its C file.  Seems to be ~okay.                                            */
/******************************************************************************/

#include <stdio.h>  /* macros may be used in files without stdio.h */
#include "usloss.h" /* console() */

extern int getpid(void);

/******************************************************************************/
/* Handy Macros                                                               */
/******************************************************************************/

extern int debugflag1;
extern int debugflag2;
extern int debugflag3;
extern int debugflag4;
extern int debugflag5;

/* least verbose (most critical) */
#define NO_DEBUG    0
#define DEBUG       1
#define DEBUG2      2
#define DEBUG3      3
#define DEBUG4      4
#define DEBUG5      5
#define DEBUG6      6
/* most verbose (least critical) */

/* control/status register masks for terminal devices. */
#define TERM_RX_INT (1 << 1)
#define TERM_TX_INT (1 << 2)

/* Flag bits for set_term_interrupt routine */
#define RX_ON  (1 << 1)
#define RX_OFF (1 << 2)
#define TX_ON  (1 << 3)
#define TX_OFF (1 << 4)

/* To de-mystify the '1' for boxes I'm using as mutexes. */
#define MUTEX_SLOTS 1

/* For MMU mapping stuff.  By default, only use a single tag */
#define DEFAULT_TAG 0
#define SWAP_DISK 1     /* unit number */

/* Useful, impossible values for mailbox and process IDs*/
#define NOT_A_BOX -1
#define NOT_A_PID -1
#define NOT_A_PAGE -1
#define NOT_ON_DISK -1
/* random virtual page number into which to map physical data from
   another process in core VM routines */
#define TEMP_PAGE 0

/* Error codes */
#define EOKAY 0
#define EBADINPUT 1
#define EMALLOC  1
#define EBADBOX  1
#define ESWAPFULL  1
#define EINIT 1
#define ENOOP 1
#define EWAITDEVICEZAPPED 1
#define EWOULDBLOCK 2
#define ENOKIDS 2
#define EZAPPED 3
#define EDEVICE 666
#define EMUCHBADNESS 42

/* integer slot ID in the process table for the current process */
#define GET_SLOT(a) ((a) % MAXPROC)
#define CURRENT GET_SLOT(getpid())

/* Conditional executable for a single function (for debugging) */
#define DEXEC(a, b) if (debugflag4 >= (a)) do { b; } while (0)

/* True if process is in kernel mode, or false if it is not. */
#define IS_IN_KERNEL (psr_get() & PSR_CURRENT_MODE)

#define KERNEL_MODE_CHECK do { \
                            if (!IS_IN_KERNEL) \
                                KERNEL_ERROR("Not in kernel mode."); \
                          } while (0)

/* Handy macro for then things go horribly wrong. Needed all too frequently. */
#define KERNEL_ERROR(format, ...) do { kernel_error(__func__, __LINE__, __FILE__, \
                                       smoosh(format, ##__VA_ARGS__ )); \
                                  } while (0)

#define KERNEL_WARNING(format, ...) do { kernel_warning(__func__, __LINE__, __FILE__, \
                                       smoosh(format, ##__VA_ARGS__ )); \
                                  } while (0)

/*!
    What's the deal?  If I call getpid(), Patrick's code explodes
    before my start4 routine even runs.  No static variable
    instantiation can somehow call this macro, so I don't see where it
    could be me if it's before my time.  Another annoying mystery.
*/

/* Handy macro for debugging.  Provides lots of nice information. */
#if 0

#define DP(level, format, ...) do { if (debugflag5 >= level && IS_IN_KERNEL) \
                                    { \
                                        console("KERNEL %25s %3d: pid %3d: ", __func__,__LINE__, getpid()); \
                                        console(format, ##__VA_ARGS__); \
                                        fflush(stdout); \
                                    } else if (debugflag5 >= level)\
                                    { \
                                        int pid; \
                                        GetPID(&pid); \
                                        console("USER   %25s %3d: pid %3d: ", __func__,__LINE__, pid); \
                                        console(format, ##__VA_ARGS__); \
                                        fflush(stdout); \
                                    } \
                                } while (0)
#else

/*!

Wed May  4 20:27:41 MST 2005

    Guess what!!  Patrick's getpid() totally fails in p1_fork() for at least
    a while.  I'm guessing that his Current pointer or whatever is NULL,
    and that he doesn't check it before derefencing it for a pid.  Nice.

    Use my fork1().  It does this check.

    Anyway, changed macro to have two flavors to correct for defect.
*/


#define DP(level, format, ...) _DP(1, level, format, ##__VA_ARGS__)
#define DP_NOPID(level, format, ...) _DP(0, level, format, ##__VA_ARGS__)

#define _DP(a, level, format, ...) do { \
                                    if (debugflag5 >= level && IS_IN_KERNEL) \
                                    { \
                                        console("KERNEL %25s %15s %3d: ", __func__, __FILE__, __LINE__); \
                                        if (a) console (" %3d ", getpid()); \
                                        console(format, ##__VA_ARGS__); \
                                        fflush(stdout); \
                                    } \
                                } while (0)
#endif

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

/* checking for zapping is frickin' tedious */
#define HANDLE_ZAPPING(ret, status, zap_code) do { \
    if (ret != -zap_code) \
        break; \
    status = -EZAPPED; \
    DP(DEBUG, "Process %d zapped\n", getpid()); \
    goto out; \
    } while (0)

/* Treat a pointer value (or whatever) as an integer */
#define INT_ME(a) ((int)(a))

#define INT_TO_POINTER(a,b) a = (void *)b
#define ZERO_POINTER(a) a = NULL

#define ADDRESS(offset) (base_address + (MMU_PageSize() * (offset)))

/* Sigh.  Had to synch MMU_* routines because I was mapping, then
 * another process was running, and mapping over the mapping I had
 * just made. :( */
#define DOWN(a, b) do { \
                    DP(DEBUG6, "Acquiring Mutex %d\n", a); \
                    int ret = MboxSend(a, &b, sizeof(b)); \
                    if (ret != EOKAY) \
                        KERNEL_ERROR("Send: Box is %d\n", a); \
                    DP(DEBUG6, "Mutex %d acquired\n", a); \
                } while (0)

#define UP(a, b) do { \
                    DP(DEBUG6, "Releasing Mutex %d\n", a); \
                    int ret = MboxReceive(a, &b, sizeof(b)); \
                    if (ret != sizeof(b)) \
                        KERNEL_ERROR("Receive: Box is %d\n", a); \
                    DP(DEBUG6, "Mutex %d released\n", a); \
                } while (0)


void disableInterrupts(void);
void enableInterrupts(void);

void bad_interrupt(int device, int unit);

void go_user_mode(void);

void kernel_error(const char *func,
                  const int line,
                  const char *file,
                  const char *message);

void kernel_warning(const char *func,
                    const int line,
                    const char *file,
                    const char *message);

char *smoosh(const char *format, ...);

void set_term_interrupts(int unit, int flags);

const char * decode_MMU_warning(int code);

#endif  // UTILITY_H

