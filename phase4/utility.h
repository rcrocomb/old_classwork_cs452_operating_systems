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

/* least verbose (most critical) */
#define NO_DEBUG    0
#define DEBUG       1
#define DEBUG2      2
#define DEBUG3      3
#define DEBUG4      4
#define DEBUG5      5
/* most verbose (least critical) */

/* control/status register masks for terminal devices. */
#define TERM_RX_INT (1 << 1)
#define TERM_TX_INT (1 << 2)

/* Flag bits for set_term_interrupt routine */
#define RX_ON  (1 << 1)
#define RX_OFF (1 << 2)
#define TX_ON  (1 << 3)
#define TX_OFF (1 << 4)

#define NOT_A_BOX -1
#define NOT_A_PID -1

/* */
#define EOKAY 0
#define EBADINPUT 1
#define EWAITDEVICEZAPPED 1
#define EWOULDBLOCK 2
#define ENOKIDS 2
#define EZAPPED 3
#define EDEVICE 666
#define ENOTBLOODYLIKELY 42

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
                                       smoosh(format, ##__VA_ARGS__ ), 1); \
                                  } while (0)

#define KERNEL_WARNING(format, ...) do { kernel_warning(__func__, __LINE__, __FILE__, \
                                       smoosh(format, ##__VA_ARGS__ )); \
                                  } while (0)

/* Handy macro for debugging.  Provides lots of nice information. */
#define DP(level, format, ...) do { if (debugflag4 >= level) \
                                    { \
                                        console("%25s %3d: pid %3d: ", __func__,__LINE__, getpid()); \
                                        console(format, ##__VA_ARGS__); \
                                        fflush(stdout); \
                                    } \
                                } while (0)

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

/******************************************************************************/
/* Utility Function Prototypes                                                */
/******************************************************************************/

void disableInterrupts(void);
void enableInterrupts(void);

void bad_interrupt(int device, int unit);

void go_user_mode(void);

void kernel_error(const char *func,
                  const int line,
                  const char *file,
                  const char *message,
                  const int do_halt);

void kernel_warning(const char *func,
                    const int line,
                    const char *file,
                    const char *message);

char *smoosh(const char *format, ...);

void set_term_interrupts(int unit, int flags);

#endif  // UTILITY_H

