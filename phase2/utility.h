#ifndef UTILITY_H
#define UTILITY_H

/******************************************************************************/
/* Author: Robert Crocombe                                                    */
/* Class: CS452 Spring 05                                                     */
/* Assignment: Phase 1                                                        */
/*                                                                            */
/* I tried to put all of the more general purpose items in this header and    */
/* its C file.  Seems to be ~okay.                                            */
/******************************************************************************/

#include <stdio.h>  /* macros may be used in files without stdio.h */
#include "usloss.h" /* console() */
#include "message.h"

/******************************************************************************/
/* Handy Macros                                                               */
/******************************************************************************/

extern int debugflag1;
extern int debugflag2;

/* least verbose (most critical) */
#define NO_DEBUG    0
#define DEBUG       1
#define DEBUG2      2
#define DEBUG3      3
#define DEBUG4      4
#define DEBUG5      5
/* most verbose (least critical) */

/* integer slot ID in the process table for the current process */
#define CURRENT (getpid() % MAXPROC)

/* Conditional executable for a single function (for debugging) */
#define DEXEC2(a, b) if (debugflag2 >= (a)) do { b; } while (0)

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

#define KERNEL_WARNING(format, ...) do { kernel_error(__func__, __LINE__, __FILE__, \
                                       smoosh(format, ##__VA_ARGS__ ), 0); \
                                  } while (0)

/* Handy macro for debugging.  Provides lots of nice information. */
#define DP2(level, format, ...) do { if (debugflag2 >= level) \
                                    { \
                                        console("%s: ", __func__); \
                                        console(format, ##__VA_ARGS__); \
                                        fflush(stdout); \
                                    } \
                                } while (0)

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

/******************************************************************************/
/* Utility Function Prototypes                                                */
/******************************************************************************/

void disableInterrupts(void);
void enableInterrupts(void);

void bad_interrupt(int device, int unit);

void kernel_error(const char *func,
                  const int line,
                  const char *file,
                  const char *message,
                  const int do_halt);


char *smoosh(const char *format, ...);

int count_links(mailbox *box);

#endif  // UTILITY_H

