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

/* Globals from phase1.c kernel file. */
extern int debugflag;
extern proc_struct *Current;
extern ll_node ReadyList[];
extern ll_node WaitList[];
extern proc_struct ProcTable[];

/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void
launch(void)
{
    int result;

    DP(DEBUG, "started: launching process '%s' pid %d priority %d \n",
                    Current->name, Current->pid, Current->priority);

    enableInterrupts();

    /* Call the function passed to fork1, and capture its return value */
    result = Current->start_func(Current->start_arg);

    DP(DEBUG, "Process '%s' pid %d priority %d returned to launch\n",
              Current->name, Current->pid, Current->priority);

    quit(result);
}

/* ------------------------------------------------------------------------
   Name - sentinel
   Purpose - The purpose of the sentinel routine is two-fold.  One
             responsibility is to keep the system going when all other
	     processes are blocked.  The other is to detect and report
	     simple deadlock states.
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in deadlock, print appropriate error
		   and halt.
   ----------------------------------------------------------------------- */
int
sentinel(char * dummy)
{
    DP(DEBUG, "called\n");

   while (1)
   {
      check_deadlock();
      waitint();
   }
}

/*
    Deadlock is considered to have occured if:
        -- There are tasks on the wait list and the sentinel is running
        (only way check_deadlock can be called currently).  Since there are
        no I/O devices, this means that a process is blocked on a condition
        that can never occur (because *everything* is blocked, or else
        sentinel wouldn't be running).

    If deadlock has occured: halt w/ error
    If deadlock hasn't occured, then all tasks are complete: halt w/ okay
*/

void
check_deadlock(void)
{
    if (!waitlist_is_empty())
        KERNEL_ERROR("deadlock: %d processes remain in sentinel",
                        count_processes());

    console("All processes completed.\n");
    halt(0);
}

/*!
    Sets Processor Status Register (PSR) Current interrupt mode bit (bit 1)
    to 1, i.e. enables USELESS interrupts.
*/

void
enableInterrupts(void)
{
    DP(DEBUG4, "Enabling interrupts\n");

    if (!IS_IN_KERNEL)
        KERNEL_ERROR("'%s' pid %d not in kernel mode.", Current->name,
                        Current->pid);
   
    psr_set(psr_get() | PSR_CURRENT_INT);

    DP(DEBUG5,"After enabling: Current == %s Previous == %s\n",
              (CURRENT_INT ?  "On" : "Off"), (PREVIOUS_INT ? "On" : "Off"));
}

/*!
    Set interrupts to previous state.  This does not work when nesting
    calls, so don't do that.  Also, it *seems* like USELESS interferes
    with my being in absolute control of the INT lines.  I had
    enableInterrupts() get called in launch, then did
    disableInterrupts() in a routine, but when I did
    restoreInterrupt() at the routine end, expecting interrupts to be
    enabled again, instead they stayed disabled.  I wasn't sure if I
    should just return in that state, relying on USELESS to help me
    out: that didn't seem particularly likely, so I gave up on doing
    this.
*/

void
restoreInterrupts(void)
{
    int psr;

    if (!IS_IN_KERNEL)
        KERNEL_ERROR("'%s' pid %d not in kernel mode.", Current->name,
                        Current->pid);    

    DP(DEBUG5,"Before restoring: Current == %s Previous == %s\n",
                  (CURRENT_INT ?  "On" : "Off"), (PREVIOUS_INT ? "On" : "Off"));

    psr = psr_get();
    if (psr & PSR_PREV_INT)
        psr_set(psr | PSR_CURRENT_INT);
    else
        psr_set(psr  & ~PSR_CURRENT_INT);

    DP(DEBUG5,"After restoring: Current == %s Previous == %s\n",
                  (CURRENT_INT ?  "On" : "Off"), (PREVIOUS_INT ? "On" : "Off"));

}

/*
    Disables interrupts by setting the Process Status Register (PSR) Current
    interrupt mode bit (bit 1) to 0.

    See restoreInterrupts() for some of my difficulties with these.
 */

void
disableInterrupts(void)
{
    int psr;
    DP(DEBUG4, "Disabling interrupts\n");

    /* turn the interrupts OFF iff we are in kernel mode */
    if (!IS_IN_KERNEL)
        KERNEL_ERROR("'%s' pid %d not in kernel mode", Current->name,
                        Current->pid);

    psr = psr_get();
    psr_set( psr_get() & ~PSR_CURRENT_INT );
    DP(DEBUG5,"After disabling: Current == %s Previous == %s\n",
              (CURRENT_INT ?  "On" : "Off"), (PREVIOUS_INT ? "On" : "Off"));
}


/*!
  Find an empty slot in the process table.  Start with the slot next to
  the slot used by the Current process.  If Current is NULL, it had
  better be at OS start, at which point the sentinel's assigned slot
  (slot 0) is returned.
*/

int
get_empty_process_slot(int sentinel)
{

    /* This is the index within ProcTable where the current process's
     * info is stored (or nonsense if Current is NULL.) */
    const int STARTING_POINT = Current ? Current - ProcTable : 0;
    int table_slot = STARTING_POINT;

    DP(DEBUG4, "Looking for process table slot\n");

    if (sentinel)
        return 0;

    /* Starting with the slot for the current process, keep checking
     * slots until (a) find an empty one (b) iterate over whole of
     * list. A pid of 0 indicates an empty slot. */
    do
    {
        table_slot = increment_slot(table_slot);
    } while ((ProcTable[table_slot].pid != 0) && (table_slot != STARTING_POINT));

    if (table_slot == STARTING_POINT)
    {
        DP(DEBUG, "'%s' pid %d -- No process slots available",
                        Current->name, Current->pid);
        return -ENOPIDS;
    }

    DP(DEBUG4, "Process table slot found as %d\n", table_slot);
    return table_slot;
}

/*!
    Note: Doesn't zero new stack area.

    Because stacks grow from high addresses to low addresses, it is
    necessary to first allocate the block of memory and get the start
    address.  The first byte of usable stack, then, is this start
    address + the size of the stack - 1.

    However, in order to free the stack at the process' ending, I keep
    an 'original pointer' (op) to the start of the area.
*/

int
initialize_process_stack(proc_struct *p, int stacksize)
{
    DP(DEBUG4, "Initializing stack for pid %d of size %d\n", p->pid, stacksize);

    char *mem = (char *)malloc(stacksize * sizeof(unsigned char));
    if (mem == NULL)
        return -EBAD;

    p->op = mem;
    p->stack = mem + stacksize - 1;
    return 0;
}

/*!
    Finds next pid that doesn't have existing process.

    I check the new pid against the current process' pid.  If that's
    okay, then I check the ready list and the list of blocked/waiting
    tasks for conflicts.  If there is no conflict there, then I check
    the list of children that have quit but are waiting to be joined.

    If there is still no conflict, then the pid is valid.

    Is this faster than just checking the entire ProcTable?  It is
    likely to have fewer checks, but there's a lot of pointer chasing.
*/

unsigned int 
find_pid(unsigned int old_pid, ll_node ready[], ll_node wait[])
{
    unsigned pid = old_pid + 1;
    proc_struct *p;
    int invalid = 0;
    int i;

    DP(DEBUG4, "Looking for new pid after old pid %d\n", old_pid);

    /* don't return until all possible pids have been checked || we've
     * found a good one (no conflicts w/ current processes) */
    do {

        /* pid 0 is reserved for empty slots, and pid 1 is sentinel pid */
        pid = (pid == 0 || pid == SENTINELPID) ? SENTINELPID + 1 : pid;

        /* Is this the same pid as the Current process? */
        invalid = Current && (pid == Current->pid);
        if (invalid)
        {
            /* Yup, try the next one. */
            ++pid;
            continue;
        }        

        /* process w/ this pid in ready or wait lists? */
        invalid = find_process(ready, pid) || find_process(wait, pid);
        if (invalid)
        {
            /* Yup, try the next one. */
            ++pid;
            continue;
        }

        /* Okay, wasn't in ready or wait lists, but could it have quit
         * and be waiting to be joined? */
        for ( i = 0; !invalid && (i < PRIO_SIZE); ++i)
        {
            /* Check every child list of every element of queue at
             * this priority for READY list*/
            p = ready[i].front; 
            while (p)
            {
                if (find_quit_kid(p))
                {
                    invalid = 1;
                    break;
                }
                p = p->next_proc_ptr;
            }

            if (invalid)
                break;

            /* Check every child list of every element of queue at
             * this priority for WAIT list*/
            p = wait[i].front; 
            while (p)
            {
                if (find_quit_kid(p))
                {
                    invalid = 1;
                    break;
                }
                p = p->next_proc_ptr;
            }
        }

        /* Well, that one was used.  Try next one. */
        if (invalid)
            ++pid;

      /* Quit if we (a) found a good pid (b) wrapped around pid list */
    } while (invalid && (pid != old_pid));

    if (pid == old_pid)
    {
        DP(DEBUG, "No free pids when old pid == %d\n", old_pid);
        return -ENOPIDS;
    }

    DP(DEBUG4, "Old pid %d, next pid is %d\n", old_pid, pid);
    return pid;
}

/*!
    Returns the next slot in the process table.

    Slots run from 0.. (MAXPROC - 1)
*/

int
increment_slot(int current_position)
{
    int slot = (current_position + 1) % MAXPROC;
    DP(DEBUG5, "New slot position is %d, old was %d\n", slot, current_position);
    return slot;
}

/*!
    Return process with pid 'pid', or NULL if no such process exists
    in the list 'list' (one of ready or wait).
*/

proc_struct *
get_proc_ptr(ll_node list[], int pid)
{
    int i = 0;
    proc_struct *p = NULL;

    DP(DEBUG3, "Looking for process with pid %d\n", pid);

    /* Check each priority queue starting w/ highest priority */
    for ( ; !p && (i < PRIO_SIZE); ++i)
    {
        /* If queue is not empty, check that queue */
        if (list[i].front)
        {
            p = list[i].front;
            while (p)
            {
                if (p->pid == pid)
                    break;
                p = p->next_proc_ptr;
            }
        }
    }
    DP(DEBUG3, "process with pid %d %s\n", pid, (p ? "Found" : "Not Found"));
    return p;
}

/*!
  Returns the next process that should be executed: starts with highest
  priority and works to lowest if no processes are present at the higher
  priorities.

  Should always find at least the sentinel process.

  The priority here is honest-to-god priority, not priority - 1

  This is similar to remove_from_list(), except here I have no
  expectation for getting a certain process: all that is given is a
  priority level at which to begin looking for a process.
*/

proc_struct *
get_from_readylist(int priority)
{
    const int index = priority - 1;
    proc_struct *p = NULL;
    int i = index;

    DP(DEBUG4, "Getting process from ReadyList\n");

    /* Execute first entry on the queue */
    for ( ; i < LOWEST_PRIORITY ; ++i)
    {
        p = ReadyList[i].front;
        if (p)
            break;
    }

    if (!p)
        KERNEL_ERROR("no process (not even sentinel)?");

    DP(DEBUG2, "Got process '%s' with pid %d priority %d\n",
                    p->name, p->pid, p->priority);

    return p;
}

/*!
    Add process 'c' to child list of process 'p'

    I must be dumb, but I had some weird errors when I used 'parent'
    and 'child' directly.  I expected the changes to them to evaporate
    at the end of the routine, since they're copies, but even
    traversing lists with them seemed to cause me errors eliminated by
    doing the copying to 'p' and 'c' as I have done.  Doesn't seem
    right.  Need to look at C standard.
*/

void
add_to_child_list(proc_struct *parent, proc_struct *child)
{
    proc_struct *travel;
    proc_struct *p = parent;
    proc_struct *c = child;

    /* Should only happen when Current is NULL: for sentinel && start1. */
    if (!p)
    {
        DP(DEBUG5, "parent pointer is NULL\n");
        return;
    }

    if (!c)
        KERNEL_ERROR("No child to add to '%s' pid %d\n", p->name, p->pid);

    if (p == c)
        KERNEL_ERROR("Making '%s' pid %d child of self", p->name, p->pid);


    DP(DEBUG3, "Adding %s pid %d to child list of %s pid %d\n",
                    c->name, c->pid, p->name, p->pid);

    c->next_sibling_ptr = NULL;
    travel = p->child_proc_ptr;
    if (!travel) /* 'p' has no current children */
    {
        p->child_proc_ptr = c;
    } else /* 'p' already has at least 1 child: add 'c' as sibling to it/them */
    {
        while(travel->next_sibling_ptr)
        {
            DP(DEBUG5, "Child is '%s' pid %d priority %d\n",
                            travel->name, travel->pid, travel->priority);
            travel = travel->next_sibling_ptr;
        }
        travel->next_sibling_ptr = c;
    }

    DP(DEBUG,"Adding: '%s' pid %d now has %d children\n", p->name,
                    p->pid, count_kids(p));
    DEXEC(DEBUG5, display_a_queue(p->child_proc_ptr, 1));
}

/*!
    Child pointed to by 'k' is removed and ProcTable entry is zeroed.
    Return pid if possible (if not zapped), and status info in 'status'.
*/

int
remove_from_child_list(proc_struct *k, int *status)
{
    int ret = -ENOKIDS;
    proc_struct *kid = k;
    proc_struct *previous = Current->child_proc_ptr;

    if (!previous)
        KERNEL_ERROR("Child list went empty for '%s' pid %d unexpectedly",
                     Current->name, Current->pid);

    /* get pointer to kid 'p - 1' if exists (else pointer to 'p') */
    while (   (previous != kid)
           && previous->next_sibling_ptr
           && (previous->next_sibling_ptr != kid))
    {
        previous = previous->next_sibling_ptr;
    }

    /* Remove child process from linked list of children */
    if (previous == kid)
    {
        /* I know thse two cases can be combined, but I get confused */
        if (kid->next_sibling_ptr == NULL)
        {
            DP(DEBUG, "Kid '%s' pid %d is only child\n", kid->name, kid->pid);
            Current->child_proc_ptr = NULL;
        } else
        {
            DP(DEBUG, "Kid '%s' pid %d is first but not only child\n",
                            kid->name, kid->pid);
            /* First but not only*/
            Current->child_proc_ptr = kid->next_sibling_ptr;
        }
    }
    else
    {
        DP(DEBUG, "Kid '%s' pid %d is not only child: %d kids\n",
                        kid->name, kid->pid, count_kids(Current));
        /* kid is not first on list of children.   Relocate link from
         * (previous to kid) to (next after kid) in list */
        previous->next_sibling_ptr = kid->next_sibling_ptr;
    }

    /* Pull kid from list */
    kid->next_sibling_ptr = NULL;

    /* Get the code on which the child quit */
    *status = kid->status;
    ret = kid->pid;

    DP(DEBUG, "After child removed, queue looks like this:\n");
    DEXEC(DEBUG, display_a_queue(Current->child_proc_ptr, 1));

    /* Cleanup kid entry */
    zeroize_proc_entry(kid);

    return ret;
}

/*!
    When a child quits, it needs to be moved to the end of its
    parent's list of children so that joins happen in the order in
    which children quit.  So that's what this routine does.
*/

void
move_to_parent_end(proc_struct *first_kid)
{
    proc_struct *p = first_kid;
    proc_struct *list_end;
    proc_struct *previous;

    /* Only child */
    if ( (p == Current) && (p->next_sibling_ptr == NULL))
        return;

    if (p == Current)
    {
        /* First but not only */
        Current->parent->child_proc_ptr = p->next_sibling_ptr;
        DP(DEBUG,"First\n");
    } else 
    {
        DP(DEBUG,"Not first\n");

        /* Not first: at least 2 children */
        previous = Current->parent->child_proc_ptr;
        while (previous && (previous->next_sibling_ptr != Current))
            previous = previous->next_sibling_ptr;

        if (!previous)
            KERNEL_ERROR("Many obscenities needed!");

        previous->next_sibling_ptr = Current->next_sibling_ptr;
    }


    /* Find end of list */
    list_end = Current->parent->child_proc_ptr;
    while (list_end->next_sibling_ptr)
        list_end = list_end->next_sibling_ptr;

    /* Point list end to Current */
    list_end->next_sibling_ptr = Current;
    /* Current is at end of list */
    Current->next_sibling_ptr = NULL;

    DP(DEBUG,"Last: parent has %d children\n", count_kids(Current->parent));
    DEXEC(DEBUG5, display_a_queue(Current->parent->child_proc_ptr, 1));

}

/*!
    If there is a child in the singly-linked list whose start is pointed to
    by 'start' with the status QUIT, a pointer to that child be returned.
    If no such child exists, then NULL is returned.
*/

proc_struct *
find_quit_kid(proc_struct *start)
{
    proc_struct *kid = start;
    while (kid)
    {
        if (status(kid, QUIT))
            break;
        kid = kid->next_sibling_ptr;
    }
    if (!kid)
        DP(DEBUG, "Found no quit but unjoined child\n");
    return kid;
}

/*!
    Re-initializes the elements of a process entry.  I decided I liked
    '0' better than '-1', and it makes the math for execution time,
    etc. simpler to get right (not off by a microsecond, for for
    example).
*/

void
zeroize_proc_entry(proc_struct *p)
{
    p->next_proc_ptr = NULL;
    p->child_proc_ptr = NULL;
    p->next_sibling_ptr = NULL;
    p->parent = NULL;
    p->zappee = NULL;
    strcpy(p->name,"");
    strcpy(p->start_arg,"");
    /* can't do context here */
    p->pid = 0;
    p->priority = -1;
    p->start_func = NULL;
    p->stack = NULL;
    SMART_FREE(p->op);  /* where stack is freed: 'op' is original pointer*/
    p->stacksize = 0;
    p->status = CLEARED_CODE;
    p->timeslice_start = 0;
    p->execution_time = 0;
    p->flags = CLEAR_FLAGS;
    p->is_zapped = NOT_ZAPPED;
}

/*!
    This is what Patrick wants in terms of info.
*/

void
dump_a_process(proc_struct *p)
{
    /*char *header = "Pid   PPid  Prio Status  #Kids  CPU(us)  Name\n";*/


    // 6 6 6 30 6 7 18 = 18 + 30 + 13 + 18 = 36 + 43 = 79 + '\n' == 80
    console("%5d %5d    %2d %30s %4d   %8d %12s\n",
            p->pid,
            p->parent ? p->parent->pid : -1,
            p->priority,
            status_to_string(p),
            count_kids(p),
            p->execution_time,/* usecs */
            p->name);
}

/*!
    Needed for dumping processes, darned handy many other places.

    Given a process 'p', counts the number of children of 'p'.
    Returns this count.
*/

int
count_kids(proc_struct *p)
{
    proc_struct *t = p->child_proc_ptr;
    int count = 0;

    if (t)
    {
        do {
            ++count;
            t = t->next_sibling_ptr;
        } while (t);
    }
    return count;
}

int
count_unquit_kids(proc_struct *p)
{
    proc_struct *t = p->child_proc_ptr;
    int count = 0;

    if (t)
    {
        do {
            count += !status(t, QUIT);
            t = t->next_sibling_ptr;
        } while (t);
    }
    return count;
}

/*!
    Cheap and stupid way to do this.  Normally I'd go through the
    lists, but I'm tired.
*/

int 
count_processes(void)
{
    int count = 0;
    int i = 0;
    for ( ; i < MAXPROC; ++i)
    {
        if (ProcTable[i].pid)
            ++count;
    }
    return count;
}


/*!
    Necessary for dump processes to make sense: converts process
    status codes to text.

    Using strncat is obnoxious.
*/

const char *
status_to_string(proc_struct *p)
{
    #define SIZE 256
    #define APPEND(a,b) strlen(strncat((a), b, SIZE - length))
    static char buffer[SIZE + 1];
    static char unknown_code_buffer[10 + 1]; // MAXINT is 10 digits
    int length = 0;

    strcpy(buffer,"");

    switch (p->flags)
    {
    case BLOCKED:
        length = snprintf(buffer, SIZE - length, "Blocked ");
        switch (code(p))
        {
        case BLOCKED_ZAPPING:
            length = APPEND(buffer, "zapping ");
            break;
        case BLOCKED_JOIN:
            length = APPEND(buffer, "joining ");
            break;
        case BLOCKED_SELF:
            length = APPEND(buffer, "self ");
            break;
        default:
            /* Display the numerical code for unknown codes */
            snprintf(unknown_code_buffer, 11, "%d ", code(p));
            length = APPEND(buffer, unknown_code_buffer);
        }
        break;

    case READY:
        length = snprintf(buffer, SIZE - length, "Ready ");
        break;

    case QUIT:
        length = snprintf(buffer, SIZE - length, "Quit ");
        break;

    case RUNNING:
        length = snprintf(buffer, SIZE - length, "Running ");
        break;

    case CLEARED_CODE:
        length = snprintf(buffer, SIZE - length, "Empty ");
        break;

    default: 
            length = snprintf(buffer, SIZE - length, "UNKNOWN! ");
            snprintf(unknown_code_buffer, 11, "%d ", code(p));
            length = APPEND(buffer, unknown_code_buffer);
    }

    if (p->is_zapped == YES_ZAPPED)
        length = APPEND(buffer, " --  zapped");
    else if (p->is_zapped == NOT_ZAPPED)
        length = APPEND(buffer, " -- !zapped");
    else
        length = APPEND(buffer, " -- EVIL!");

    return buffer;
}

/*!
    Adds all kinds of goodies about kernel errors before calling
    console and halt: function from which this was called, the
    filename, and the line in that file.
*/

void
kernel_error(const char *func, const int line, const char *file, const char *message)
{
    console("%s(): on line %d in %s, error is:\n%s\nHalting!\n",
            func, line, file, message);
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

/*!
    If timeslice has expired, call dispatcher.  Guard against my
    inadvertantly enabling interrupts before I've pointed Current to a
    task.
*/

void
clock_handler(int dev, int unit)
{
    if (!Current)
        return;

    time_slice();
}

/*!
    Error routine to catch interrupts from unexpected sources.
*/

void
bad_interrupt(int dev, int unit)
{
    console("Bad interrupt for dev %d unit %d\n", dev, unit);
}

/*!
    Adds 'p' to the end of the proper queue (for its priority) of list
    'list' (ready or wait).
*/

void
add_to_list(ll_node list[], proc_struct *p)
{
    int index;
    if (!p)
        KERNEL_ERROR("process to add is NULL");

    index = p->priority - 1;

    DP(DEBUG, "adding '%s' pid %d w/ priority %d on %s list\n", p->name,
                    p->pid, p->priority, ((list == ReadyList) ? "READY"
                    : "WAIT"));

    if (list[index].back)
    {
        DP(DEBUG5, "queue for priority %d ISN'T empty\n", p->priority);
        DEXEC(DEBUG5, display_a_queue(list[index].front, 0));
        list[index].back->next_proc_ptr = p;
        p->next_proc_ptr = NULL;
        list[index].back = p;
    }
    else
    {
        DP(DEBUG5, "queue for priority %d IS empty\n", p->priority);
        DEXEC(DEBUG5, display_a_queue(list[index].front, 0));
        /* queue is empty */
        list[index].front = p;
        list[index].back = p;
        p->next_proc_ptr = NULL;
    }
    DP(DEBUG5, "Queue after adding process '%s'\n", p->name);
    DEXEC(DEBUG5, display_a_queue(list[index].front, 0));
}

/*!
    Removes first element from the queue at the given priority, or NULL if
    there is no task (queue is empty).  I expect this task to be equal
    to that pointed to by 'process'.  I really only needed a pid, but
    the usage always ended up being specific to a certain task, so I
    decided to pass that instead.  Meh.

    Degenerate case of remove_from_within_list.
*/

proc_struct *
remove_from_list(ll_node list[], proc_struct *process)
{
    int index;
    proc_struct *c;
    proc_struct *p = process;

    if (!p)
        KERNEL_ERROR("pointer was NULL");

    index = p->priority - 1;

    /* Queue is empty */
    if (!list[index].front)
    {
        DP(DEBUG4, "List empty removing task with priority %d\n", p->priority);
        return NULL;
    }

    disableInterrupts();

    DEXEC(DEBUG5, display_a_queue(list[index].front, 0));

    /* Point 'c' to 1st element of queue, advance 'front' ptr to next element.*/
    c = list[index].front;
    list[index].front = list[index].front->next_proc_ptr;

    /* If queue is now empty then 'back' pointer of queue need also
     * point to NULL. */
    if (c == list[index].back)
        list[index].back = NULL;

    DP(DEBUG3, "Removing process '%s' pid %d priority %d from %s list\n",
                    c->name, c->pid, c->priority, ((list == ReadyList)
                    ? "READY": "WAIT"));

    DEXEC(DEBUG5, display_a_queue(list[index].front, 0));
    enableInterrupts();

    return c;
}

/*!
    Find process 'proc' within the list 'list' and remove it.  This is
    useful for the wait list, where there is little guarantee about
    where a process that we're interested in might be (as opposed to
    ready list, where things are on the front).

    If no such process is found, return NULL.
*/

proc_struct *
remove_from_within_list(ll_node list[], proc_struct *proc)
{
    proc_struct *p = NULL;
    proc_struct *previous = NULL;
    int i = proc->priority - 1;

    if (!proc)
        KERNEL_ERROR("process pointer is NULL\n");

    DP(DEBUG3, "Removing '%s' pid %d priority %d from list\n",
                    proc->name, proc->pid, proc->priority);

    /* Find proc and element previous to it in queue */
    if (list[i].front)
    {
        previous = p = list[i].front;
        while (p && (p != proc))
        {
            previous = p;
            p = p->next_proc_ptr;
        }
    }

    /* Remove element from list */
    if (p)
        break_list(list, i, p, previous);

    return p;
}

/*!
    Reforms the queue in list 'list' at index 'i' (not priority 'i').
    'p' is the element we're reforming out (removing from queue), and
    'previous' is element 'p - 1' in the queue.
*/

void
break_list(ll_node list[], int i,proc_struct * const p, proc_struct * const previous)
{
    DP(DEBUG5,"break list for '%s'\n", p->name);

    /* Found node and previous node: rejoin list around 'p' */
    if (p == previous)
    {
        DP(DEBUG5,"first\n");
        /* First in queue */
        list[i].front = list[i].front->next_proc_ptr;
        /* Only in queue? */
        if (list[i].back == p)
            list[i].back = NULL;
    } else
    {
        DP(DEBUG5,"not first\n");
        /* Not first (and therefore not only) */
        previous->next_proc_ptr = p->next_proc_ptr;
        /* Last? */
        if (p == list[i].back)
            list[i].back = previous;
    }
}

/*!
    Checks all entries of all queues in list for a proces with pid
    'pid'.  Returns pointer to process if found, or NULL if not.
*/

proc_struct *
find_process(ll_node list[], int pid)
{
    proc_struct *p = NULL;
    int i = HIGHEST_PRIORITY - 1;

    /* For each queue of priority or lower priority */
    for ( ; !p && (i < PRIO_SIZE); ++i)
        /* If there are entries in the queue */
        if (list[i].front)
        {
            /* Look for an entry with pid == 'pid' */
            p = list[i].front;
            while (p && (p->pid != pid))
                p = p->next_proc_ptr;
            if (p)
                break;
        }

    /* Non-NULL if process was found, else NULL*/
    return p;
}


/*!
    Starting with queues at priority 'priority', return true if there is a
    task of priority or lower, or false if there isn't.
*/

int
list_is_empty(ll_node list[], int priority)
{
    int i  = priority - 1;
    for ( ; i < PRIO_SIZE; ++i)
        if (list[i].front)
            break;
    return i == PRIO_SIZE;
}

/*!
    Finds processes that have blocked on zapping process 'p', which is
    quitting, and unblocks them.
*/

void
unblock_zappers(proc_struct *p)
{
    int i = 0;
    proc_struct *q;
    proc_struct *previous;

    /* For all priorities */
    for ( ; i < PRIO_SIZE; ++i)
    {
        /* if queue at this priority isn't empty */
        if (WaitList[i].front)
        {
            q = WaitList[i].front;
            previous = q;
            /* For this priority queue: see if anyone has zapped 'p' */
            while (q)
            {
                if (q->zappee == p)
                {
                    /* 'q' is a zapper of 'p': unblock it */
                    code(q) = CLEARED_CODE;
                    set_status(q, READY);

                    /* Remove from WaitList: mayn't be first in queue, so
                     * this is extra work. */
                    break_list(WaitList, i, q, previous);

                    /* Remove q's link to the list, if any */
                    q->next_proc_ptr = NULL;

                    /* add 'q' to readylist */
                    add_to_readylist(q);

                    /* So can continue looking for additional
                     * elements: start from front so I don't screw up
                     * logic of continuing from current spot
                     * XXX: slower! */
                    previous = q = WaitList[i].front;
                } else
                {
                    /* Look for next element*/
                    previous = q;
                    q = q->next_proc_ptr;
                }
            }
        }
    }
}

/*!
    For all transitions of process status (to and from wait and ready
    list, from the ready list to running, etc.), use this routine.  I
    have tried to encode into it my ideas of legal transitions.  If it
    gets yuckier, I should see about using function pointers or
    something.

    Maybe I should throw yet another level of macros into this?  Hee hee.

    #define BOOM(a, b, c) do { \
                 KERNEL_ERROR("%s process %s pid %d transitioning to %s?", \
                              b, (a)->name, (a)->pid, c); \
                          } while(0)

    BOOM(p, "blocked", "ready");
*/

void
set_status(proc_struct *p, const int value)
{
    if (!p)
        KERNEL_ERROR("NULL process pointer");

    /* Want we want process to become */
    switch (value)
    {
    case RUNNING:
        switch (p->flags)
        {
        /* All scheduable tasks must pass through ready list */
        case RUNNING:
            KERNEL_ERROR("RUNNING -> RUNNING for '%s' pid %d\n",
                         p->name, p->pid);
            break;

        /* Only READY->RUNNING is acceptable transition */
        case READY:
            p->flags = RUNNING;
            break;
        case BLOCKED:
            KERNEL_ERROR("blocked task '%s' pid %d moved to running?\n",
                         p->name, p->pid);
            break;
        case QUIT:
            KERNEL_ERROR("quit task '%s' pid %d moved to running?\n",
                         p->name, p->pid);
            break;
        default:
            KERNEL_ERROR("task in UNKNOWN state '%s' pid %d moved to running?\n",
                         p->name, p->pid);
        }
        break;

    case BLOCKED:
        /* What process is currently */
        switch (p->flags)
        {
        case RUNNING:
            p->flags = BLOCKED;
            break;
        case READY:
            KERNEL_ERROR("ready task %d is blocking? "
                         "Status currently '%s'", p->pid, status_to_string(p));
        case BLOCKED:
            KERNEL_ERROR("blocked task %d is blocking again? "
                         "Status currently '%s'", p->pid, status_to_string(p));
            break;
        case QUIT:
            KERNEL_ERROR("setting status to BLOCKED for "
                         "task that has quit: pid == %d", p->pid);
            break;
        default:
            KERNEL_ERROR("status to BLOCKED for "
                         "process that has unknown status %08x", p->flags);
        }
        break;

    case READY:
        /* What process is currently */
        switch (p->flags)
        {
        case RUNNING:           /* okay, happens at end of normal time slice*/
        case CLEAR_FLAGS:       /* fall-through */
        case BLOCKED:
            p->flags = READY;
            break;
        case READY:
            /* How could this happen? */
            console("marking ready task %d ready", p->pid);
            break;
        case QUIT:
            KERNEL_ERROR("marking ready a quit task %d", p->pid);
            break;
        default:
            KERNEL_ERROR("marking ready unknown task status: %08x", p->flags);
        }
        break;

    case QUIT:
        /* What process is currently */
        switch (p->flags)
        {
        case RUNNING:
            /* Seems okay: running task marking itself as quit. */
            p->flags = QUIT;
            break;
        case BLOCKED:
            KERNEL_ERROR("Blocked task '%s' pid %d setting status to quit?",
                            p->name, p->pid);
            break;
        case READY:
            KERNEL_ERROR("Ready task '%s' pid %d setting status to quit?",
                            p->name, p->pid);
            break;
        case QUIT:
            KERNEL_ERROR("quitting task '%s' pid %d that has already quit",
                            p->name, p->pid);
            break;
        default:
            KERNEL_ERROR("trying to QUIT task '%s' pid %d with unknown status %08x",
                            p->name, p->pid, value);
        }
        break;

    default:
        KERNEL_ERROR("trying to set unknown status %08x for task %d",
                        value, p->pid);
    }

    DP(DEBUG, "Status of '%s' pid %d after setting status is '%s'\n",
              p->name, p->pid, status_to_string(p));
}

/*!
    Never used routine to initialize proc table.  I've decided that
    having all values set to 0 by the compiler is fine.
*/

void
init_proctable(void)
{
    int i = 0;
    for ( ; i < MAXPROC; ++i)
        zeroize_proc_entry(ProcTable + i);
}

/*!
    Debugging-only routine that displays all the info about a process
    (more than dump_a_process()).
*/

void
display_raw_proc(proc_struct *p)
{
    console("name %s\npid %d\nprio %d\nstacksize %d status %d timeslice start %d execution time %d\n"
            "next %08x\nchild %08x\nsibling %08x\nparent %08x\nzappee %08x\n",
            p->name, p->pid, p->priority, p->stacksize, p->status,
            p->timeslice_start, p->execution_time,
            p->next_proc_ptr, p->child_proc_ptr, p->next_sibling_ptr,
            p->parent, p->zappee);
}

/*!
    For displaying all the information about a particular queue.

    If the queue is a list of children (using next_sibling_ptr), then
    'is_child_list' should be non-zero, else I'll assume it's a
    regular (ready or wait) list and use 'next_proc_ptr'.
*/

void
display_a_queue(proc_struct *p, int is_child_list)
{
    #define next(a, b) do { if (b) \
                                (a) = (a)->next_sibling_ptr; \
                            else \
                                (a) = (a)->next_proc_ptr; \
                        } while (0)

    if (!p)
    {
        DP(DEBUG, "Empty list\n");
        return;
    }

    while (p)
    {
        display_raw_proc(p);
        next(p, is_child_list);
    }
}

