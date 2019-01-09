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

#include "kernel.h"

#include <stdio.h>  /* macros may be used in files without stdio.h */

/******************************************************************************/
/* Handy Macros                                                               */
/******************************************************************************/

#define ENABLE_INTERRUPTS do { \
                                if (!Current) \
                                    KERNEL_ERROR("Show me a line!"); \
                                enableInterrupts(); \
                        } while(0)

extern int debugflag;

/* least verbose (most critical) */
#define NO_DEBUG    0
#define DEBUG       1
#define DEBUG2      2
#define DEBUG3      3
#define DEBUG4      4
#define DEBUG5      5
/* most verbose (least critical) */

/* Conditional executable for a single function (for debugging) */
#define DEXEC(a, b) if (debugflag >= (a)) do { b; } while (0)

/* I find this mnemonic more memorable for for() loops. */
#define PRIO_SIZE LOWEST_PRIORITY

/* More compact, although does can require an extra psr_get() :( */
#define CURRENT_INT (psr_get() & PSR_CURRENT_INT)
#define PREVIOUS_INT (psr_get() & PSR_PREV_INT)

/* True if process is in kernel mode, or false if it is not. */
#define IS_IN_KERNEL (psr_get() & PSR_CURRENT_MODE)

/* Handy macro for then things go horribly wrong. Needed all too frequently. */
#define KERNEL_ERROR(format, ...) do { kernel_error(__func__, __LINE__, __FILE__, \
                                       smoosh(format, ##__VA_ARGS__ )); \
                                  } while (0)

/* Handy macro for debugging.  Provides lots of nice information. */
#define DP(level, format, ...) do { if (debugflag >= level) \
                                    { \
                                        console("%s: ", __func__); \
                                        console(format, ##__VA_ARGS__); \
                                        fflush(stdout); \
                                    } \
                                } while (0)

/* For when I need to convert from clock-level accuracy to that
 * demanded by various functions. */
#define US_TO_MS(a) ((a) / 1000UL)

/* Handy shorthand to provide arguments to more general purpose functions. */
#define add_to_readylist(a) add_to_list(ReadyList, a)
#define add_to_waitlist(a) add_to_list(WaitList, a)

#define remove_from_readylist(a) remove_from_list(ReadyList, a)
#define remove_from_waitlist(a) remove_from_within_list(WaitList, a)

#define waitlist_is_empty() list_is_empty(WaitList, HIGHEST_PRIORITY)

/******************************************************************************/
/* Utility Function Prototypes                                                */
/*  Wow, rathers lots of the little buggers, aren't there?                    */
/******************************************************************************/

void launch(void);
int sentinel(char *);

void check_deadlock(void);
void enableInterrupts(void);
void restoreInterrupts(void);
void disableInterrupts(void);

int get_empty_process_slot(int sentinel);
int initialize_process_stack(proc_struct * proc, int stacksize);
unsigned int find_pid(unsigned int old_pid, ll_node ready[], ll_node wait[]);
int increment_slot(int current_position);

proc_struct *get_proc_ptr(ll_node list[], int pid);
proc_struct *get_from_readylist(int priority);

void add_to_child_list(proc_struct *p, proc_struct *c);
int remove_from_child_list(proc_struct *k, int *status);
void move_to_parent_end(proc_struct *first_kid);
proc_struct *find_quit_kid(proc_struct *p);

void zeroize_proc_entry(proc_struct *p);
void dump_a_process(proc_struct * p);
int count_kids(proc_struct *p);
int count_unquit_kids(proc_struct *p);
int count_processes(void);
const char *status_to_string(proc_struct *p);

void kernel_error(const char *func,
                  const int line,
                  const char *file,
                  const char *message);
char *smoosh(const char *format, ...);

void clock_handler(int dev, int unit);
void bad_interrupt(int dev, int unit);

void add_to_list(ll_node list[], proc_struct * p);
proc_struct *remove_from_list(ll_node list[], proc_struct *p);
proc_struct *remove_from_within_list(ll_node list[], proc_struct *proc);
void break_list(ll_node list[],
                int i,
                proc_struct * const p,
                proc_struct * const previous);
proc_struct *find_process(ll_node list[], int pid);
int list_is_empty(ll_node list[], int priority);
void unblock_zappers(proc_struct *p);
void set_status(proc_struct *p, const int value);

void init_proctable(void);

void display_raw_proc(proc_struct *p);
void display_a_queue(proc_struct *p, int is_child_list);

#endif  // UTILITY_H

