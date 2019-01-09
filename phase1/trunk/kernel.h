#ifndef KERNEL_H
#define KERNEL_H

#include <stdlib.h>

#include "phase1.h"             /* MAXNAME, other stuff */

#define SMART_FREE(a) if(a){free(a); a = NULL; }

/* from description of routine block_me() */
#define MIN_BLOCK_CODE 10

/* Various error codes */
#define EBAD        1
#define EZAPPED     1
#define ENOPIDS     1
#define ETINYSTACK  2
#define ENOKIDS     2
#define EBADPID     2

/* Must be > MIN_BLOCK_CODE, which is 10 decimal (see above) */
#define CLEARED_CODE        0

#define BLOCKED_ZAPPING   20
#define BLOCKED_JOIN      21
#define BLOCKED_SELF      22

/* For flags field: keep associated codes in "status" */
/* All 0s means ProcTable entry not in use. */
#define CLEAR_FLAGS 0
#define BLOCKED     1
#define READY       2
#define QUIT        3
#define RUNNING     4

#define YES_ZAPPED 1
#define NOT_ZAPPED 0

/* Kind of a misleading name, but not really*/
#define status(a, b) (a->flags == b)
#define code(a) (a->status)

typedef struct _proc_struct proc_struct;
typedef int(*process_func_t)(char *);
typedef proc_struct *proc_ptr;

struct _proc_struct
{
   proc_struct *next_proc_ptr;
   proc_struct *child_proc_ptr;
   proc_struct *next_sibling_ptr;
   proc_struct *parent;
   proc_struct *zappee;           /* pointer to process we're zapping */
   char           name[MAXNAME];     /* process's name */
   char           start_arg[MAXARG]; /* args passed to process */
   context        state;             /* current context for process */
   short          pid;               /* process id */
   int            priority;
   int (* start_func) (char *);   /* function where process begins -- launch */
   char          *stack;
   char          *op;
   unsigned int   stacksize;
   int            status;           /* codes associated with QUIT, BLOCKED */
   int            timeslice_start;  /* in useconds */
   int            execution_time;   /* in useconds */

   unsigned char  flags;            /* READY, BLOCKED, QUIT, etc. */
   unsigned char  is_zapped;

};

typedef struct
{
    proc_struct *front;
    proc_struct *back;
} ll_node;


struct psr_bits
{
   unsigned int unused:28;
   unsigned int prev_int_enable:1;
   unsigned int prev_mode:1;
   unsigned int cur_int_enable:1;
   unsigned int cur_mode:1;
};

union psr_values
{
   struct psr_bits bits;
   unsigned int integer_part;
};

/* Some useful constants.  Add more as needed... */
#define NO_CURRENT_PROCESS NULL
#define MINPRIORITY 5
#define MAXPRIORITY 1
#define SENTINELPID 1
#define SENTINELPRIORITY LOWEST_PRIORITY

#endif  //  KERNEL_H

