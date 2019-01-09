#ifndef HELPER_H
#define HELPER_H

/*!
    Author: Robert Crocombe
    Class: CS452 Spring 05
    Assignment: Phase 3

    Pretty much everything ended up here.  Only a few routines are
    externally visible, and a couple of those only grudingly so:
    basically, whatever is required by start2.  Everything else is
    static and visible only within helper.c.
*/

#include <phase1.h>
#include <phase2.h>
#include <phase3.h>

/******************************************************************************/
/* Types and the like                                                         */
/******************************************************************************/

typedef int (*func_p)(char *);

/*!
    Because I can't peek inside a mailbox to see how many slots are
    used up (and the conditional send uses up a slot, so it's not a valid
    substitute), I keep track of how many "Ps" and "Vs" (or "downs" and
    "ups" to normal folk) have been done within this struct.  These values
    are necessary to see if processes are blocked (count is < 0) when
    freeing a semaphore.

    max_count makes sure that excessive "V-ing" doesn't free the
    semaphore beyond what's possible.
*/

typedef struct _semaphore_struct
{
    int box_ID;
    int count;
    int max_count;
} semaphore_t;

/*!
    Process table struct for Phase 3.  'pid' is the process ID of the
    process that the entry is for, 'ppid' is the parent of this process,
    box_ID is the ID of a mutex mailbox used for syncronization with parent
    process.  'kids_front' and 'kids_back' are for keeping track,
    via a queue, of the children of this task.  'next' allows this element
    to be part of such a queue.
*/

typedef struct _proc_struct
{
    int pid;
    int ppid;
    int box_ID; /* Only for process's that have children */
    struct _proc_struct *kids_front, *kids_back, *next;
    func_p func;
    char name[MAXNAME];
} proc_struct_t;

/******************************************************************************/
/* Globals                                                                    */
/******************************************************************************/

semaphore_t semaphore_table[MAXSEMS];
proc_struct_t process_table[MAXPROC];

/******************************************************************************/
/* Prototypes                                                                 */
/******************************************************************************/

/* Poop.  Exposed (not static) because of start2. */
int spawn_real(char *name, func_p f, char *arg, int stack_size, int priority);
int wait_real(int *status);

/* Needed in start2 as well. */
void initialize_process_table(void);
void initialize_semaphore_table(void);
void initialize_sys_vec(void);

#endif  /* HELPER_H */

