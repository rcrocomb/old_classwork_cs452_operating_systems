/* ------------------------------------------------------------------------
   phase1.c

    University of Arizona
    Computer Science 452
    Spring 2005

    Robert Crocombe

   ------------------------------------------------------------------------ */

#include "kernel.h"
#include "utility.h"
#include "phase1.h"

#include <string.h>

extern int start1 (char *);

/* -------------------------- Globals ------------------------------------- */

int debugflag = NO_DEBUG;

/* the process table */
proc_struct ProcTable[MAXPROC];

/* Zeroed because it's global, see 6.7.8-10 of C99 standard */
ll_node ReadyList[PRIO_SIZE];   /* ready to execute tasks go here */
ll_node WaitList[PRIO_SIZE];    /* blocked or sleeping tasks go here*/

/* current process ID */
proc_struct *Current;

/* the next pid to be assigned */
unsigned int next_pid = SENTINELPID;


/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
	     Start up sentinel process and the test process.
   Parameters - none, called by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void
startup(void)
{
    int i = 0;
    int result; /* value returned by call to fork1() */

   /* initialize the process table -- zeroed is fine */
   /* Initialize the Ready list: naught to do -- is fine */

   /* Initialize the clock interrupt handler */
   for ( ; i < NUM_INTS; ++i)
        int_vec[i] = bad_interrupt;

   int_vec[CLOCK_DEV] = clock_handler;
   
   /* startup a sentinel process */
    DP(DEBUG3, "calling fork1() for sentinel\n");

   result = fork1("sentinel", sentinel, NULL,USLOSS_MIN_STACK,SENTINELPRIORITY);
   if (result < 0)
         KERNEL_ERROR("fork1 of sentinel returned error");

    /* start the test process */
    DP(DEBUG3, "calling fork1() for start1\n");

    /* XXX: Uh, this was set as '2 *', but the instructions say 32kB
     * of stack, which only '4 *' will get us (min being 8kB). */
    result = fork1("start1", start1, NULL, 4 * USLOSS_MIN_STACK, 1);
    if (result < 0)
        KERNEL_ERROR("fork1 for start1 returned error %d", result);

    console("Should not see this message! ");
    console("Returned from fork1 call that created start1\n");
}


/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */

void
finish(void)
{
    DP(DEBUG,"in finish...\n");
}

/* ------------------------------------------------------------------------
   Name - fork1
   Purpose - Gets a new process from the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.
   Parameters - the process procedure address, the size of the stack and
                the priority to be assigned to the child process.
   Returns - the process id of the created child or -1 if no child could
             be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed

    I did the checks for argument validity from left to right, as
    possible
    - first for 'name' (several tests)
    - second for 'f'
    - nothing for 'arg' initially
    - stacksize
    - priority

   ------------------------------------------------------------------------ */
int
fork1(char *name, process_func_t f, char *arg, int stacksize, int priority)
{
   int proc_slot = 0;
   proc_struct *new_entry = NULL;

   /* test if in kernel mode; halt if in user mode */
    if (!IS_IN_KERNEL)
        KERNEL_ERROR("'%s' pid %d is not in kernel mode", Current->name, Current->pid);

    DP(DEBUG2, "creating process %s\n", name ? name : "NULL");

    /* strlen() will segfault on a NULL pointer. */
    if (!name)
        console("you are mean.  No process name provided.\n");

   /* check for valid length for process name */
   if (strlen(name) > (MAXNAME - 1))
      KERNEL_ERROR("Process name is too long.");

    /* check for valid function pointer */
    if (!f)
    {
        DP(DEBUG, "Invalid function pointer for '%s'\n", name);
        return -EBAD;
    }

   /* Return if stack size is too small */
    if (stacksize < USLOSS_MIN_STACK)
    {
        DP(DEBUG, "Too small stack for '%s'\n", name);
        return -ETINYSTACK;
    }

    /* check for valid priority */
   if ((priority < HIGHEST_PRIORITY ) || (priority > LOWEST_PRIORITY))
   {
        DP(DEBUG, "Invalid priority %d for '%s'\n", priority, name);
        return -EBAD;
   }

    /* disable interrupts before examining or modifing structures to
     * avoid their being in an inconsistent state. */
    disableInterrupts();

    DP(DEBUG3, "Finding empty process slot\n");

   /* find an empty slot in the process table */
    proc_slot = get_empty_process_slot(f == sentinel);
    if (proc_slot < 0)
    {
        DP(DEBUG, "No empty process slots for '%s'", name);
        ENABLE_INTERRUPTS;
        return proc_slot;   /* no empty slots */ 
    }

    new_entry = ProcTable + proc_slot;

    strncpy(new_entry->name, name, MAXNAME - 1);

    /* NULL arg is okay. */
    if (!arg)
        strcpy(new_entry->start_arg,"");
    else if (strlen(arg) > (MAXARG - 1))
        KERNEL_ERROR("argument too long.");
    else
        strncpy(new_entry->start_arg, arg, MAXNAME - 1);

    new_entry->pid = next_pid;
    next_pid = find_pid(next_pid, ReadyList, WaitList);
    if (next_pid == -ENOPIDS)
    {
        DP(DEBUG, "No pids remain for '%s'", name);
        ENABLE_INTERRUPTS;
        return -ENOPIDS;     
    }

    /* Put this last so I don't have to keep doing free()s if other
     * elements fail. */
    if (initialize_process_stack(new_entry, stacksize) != 0)
    {
        ENABLE_INTERRUPTS;
        return -EBAD;
    }

   /* Initialize context for this process, but use launch function pointer for
    * the initial value of the process's program counter (PC)
    */
    DP(DEBUG4, "Calling context init for process '%s'\n", name);
   context_init( &(new_entry->state), psr_get(), new_entry->stack, launch);


    new_entry->next_sibling_ptr = NULL;
    new_entry->parent = Current;
    new_entry->zappee = NULL;
    new_entry->priority = priority;
    new_entry->start_func = f;
    new_entry->stacksize = stacksize;
    code(new_entry) = CLEARED_CODE;
    new_entry->timeslice_start = 0;
    new_entry->execution_time = 0;
    set_status(new_entry, READY);
    new_entry->is_zapped = NOT_ZAPPED;

    add_to_child_list(Current, new_entry);

    add_to_readylist(new_entry);

    /* for future phase(s) */
    p1_fork(new_entry->pid);

    DP(DEBUG, "complete: new process '%s', pid %d  wth arg '%s'\n",
              name, new_entry->pid, new_entry->start_arg);

    /* done modifying ProcTable entry: restore interrupts */

#if 1
    /* Not before sentinel and startup1 are working, or clock tick
     * will cause a crash because Current is still NULL.  I tried
     * using a "restoreInterrupts" routine to simply move PREV_INT to
     * CUR_INT, but the USELESS library apparently futzes with the PSR
     * register in mysterious ways, so that after an
     * ENABLE_INTERRUPTS in launch(), when next a kernel routine runs
     * I can run disableInterrupts() and see that both current and
     * *previous* interrupt states are 'off'.  Or I'm confused.
     *
     * The reason why that would be good is that if we entered this
     * routine with interrupts off (when called by startup), then
     * calling restoreInterrupts() could be unconditional.
     *
     * Anyway, this is needed. */
    if (Current)
        ENABLE_INTERRUPTS;
#else
        ENABLE_INTERRUPTS;
#endif

    if (Current)
        DP(DEBUG, "'%s' pid %d priority %d calling dispatcher from fork1\n",
                  Current->name, Current->pid, Current->priority);
              
    dispatcher();

    return new_entry->pid;
}

/* ------------------------------------------------------------------------
   Name - join
   Purpose - Wait for a child process (if one has been forked) to quit.  If 
             one has already quit, don't wait.
   Parameters - a pointer to an int where the termination code of the 
                quitting process is to be stored.
   Returns - the process id of the quitting child joined on.
		-1 if the process was zapped in the join
		-2 if the process has no children
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and blocked.

    I disable interrupts so that the list is not changed while we are
    scanning it.  My example scenario is: parent is 1 billion threads (not
    processes: so all have same pid) that has 1 child process.  Every parent
    thread calls join nigh-simultaneously.  There could be parents in join
    who are removing the child from their list of unjoined child processes
    even while other threads are trying to locate the child.

    By disabling interrupts, we ensure that only 1 parent does this
    manipulation.  Or so I keep telling myself.

    ** join returns info in the order in which children quit by putting
    quitters at the end of the list of children (where they belong!).

   ------------------------------------------------------------------------ */


int
join(int *status)
{
    int ret = -ENOKIDS;
    proc_struct *kid = NULL;

    DP(DEBUG, "Process '%s', pid %d called join, has %d children\n",
                    Current->name,Current->pid, count_kids(Current));
    DEXEC(DEBUG, display_a_queue(Current->child_proc_ptr, 1));

    if (!IS_IN_KERNEL)
        KERNEL_ERROR("'%s' pid %d is not in kernel mode", Current->name, Current->pid);

    if (!Current->child_proc_ptr)
        return -ENOKIDS;

    /* Check to see if there're any kids who've quit already */
    do
    {
        disableInterrupts();
        kid = find_quit_kid(Current->child_proc_ptr);
        if (!kid)
        {
            ENABLE_INTERRUPTS;
            /* No unjoined children that have quit: block until this happens */
            block_me(BLOCKED_JOIN);
        }
    } while (!kid);

    /* Interrupts are disabled here */

    DP(DEBUG, "First quit child is %s, %s with pid %d, removing from list of children\n", Current->name, kid->name, kid->pid);

    /* Remove the child 'kid' from Current's child list.*/
    ret = remove_from_child_list(kid, status);

    DP(DEBUG, "'%s' join complete: %d kids remain : status is %d\n",
                    Current->name, count_kids(Current), *status);

    if (is_zapped())
        DP(DEBUG3, "'%s' was zapped while joining\n");

    /* If zapped while joining, return EZAPPED and not kid's pid */
    ENABLE_INTERRUPTS;
    return is_zapped() ? -EZAPPED : ret;
}

/* ------------------------------------------------------------------------
   Name - quit
   Purpose - Stops the child process and notifies the parent of the death by
    putting child quit info on the parents child completion code list.

   Parameters - the code to return to the grieving parent
   Returns - nothing
   Side Effects - changes the parent of pid child completion status list.

   When a child quits, it is moved to the end of the list of children
   for its parent.  This enables the condition that joins must happen
   in the order in which children quit, since children that quit later
   will be put after children that quit first.

   ------------------------------------------------------------------------ */

void
quit(int code)
{
    proc_struct *p = NULL;

    if (!Current)
        KERNEL_ERROR("Current is NULL!");

    DP(DEBUG, "Process '%s' with pid %d code %d is quitting\n", Current->name, Current->pid, code);

    DEXEC(DEBUG5, display_raw_proc(Current));

    if (!IS_IN_KERNEL)
        KERNEL_ERROR("'%s' pid %d is not in kernel mode", Current->name, Current->pid);

    /* Cannot quit if have unquit kids */
    if (count_unquit_kids(Current))
        KERNEL_ERROR("Process '%s' pid %d has %d unquit children!",
                        Current->name, Current->pid,count_unquit_kids(Current));

    /* Modifying list of children: disable interrupts */
    disableInterrupts();
    Current->execution_time += sys_clock() - Current->timeslice_start;

    /* start1 and sentinel only */
    if (!Current->parent)
        goto parentless_quit;

    DP(DEBUG3, "Moving '%s' to end of parent's - '%s' pid '%d' - child list\n",
               Current->name, Current->parent->name, Current->parent->pid);

    p = Current->parent;
    if (!Current->parent->child_proc_ptr)
        KERNEL_ERROR("Process '%s' pid %d priority %d has parent '%s' "
                     "pid %d priority %d but parent has no child list\n",
                     Current->name, Current->pid, Current->priority,
                     p->name, p->pid, p->priority);

    DP(DEBUG, "Parent '%s' pid %d has %d children\n",
              Current->parent->name, Current->parent->pid,
              count_kids(Current->parent));

    move_to_parent_end(Current->parent->child_proc_ptr);

    /* Unblock parent if necessary */
    if (   status(Current->parent, BLOCKED)
        && (code(Current->parent) == BLOCKED_JOIN))
    {
        /* Cannot use unblock_proc here because it's for JOIN. */
        DP(DEBUG2, "Unblocking joined parent '%s'\n", Current->parent->name);
        code(Current->parent) = CLEARED_CODE;
        set_status(Current->parent, READY);
        p = remove_from_waitlist(Current->parent);
        add_to_readylist(p);
        DP(DEBUG, "Parent '%s' pid %d unblocked\n", p->name, p->pid);
    }

parentless_quit:    

    /* destroy process stack, most cleanup must wait on 'join'. */
    SMART_FREE(Current->op);

    /* Unblock people who've zapped this task and are blocked */
    if (is_zapped())
        unblock_zappers(Current);

    /* Mark self as quit */
    set_status(Current, QUIT);
    Current->status = code;

    p1_quit(Current->pid);

    DP(DEBUG, "Quit complete for '%s' pid %d\n", Current->name, Current->pid);
    if (Current->parent) DEXEC(DEBUG, display_a_queue(Current->parent->child_proc_ptr, 1));

    ENABLE_INTERRUPTS;
    dispatcher();
}


/*!
    Mark a task as zapped.  This will be an indicator that a task
    needs to quit as soon as it is able -- done this way so that tasks
    aren't removed while in the middle of handling something
    important, but are given a chance to notice that their quitting is
    'desirable' and they should clean up and stop.

    Zappers do not return until zappees have quit.

    block_me() will return -EZAPPED into 'ret' if the process was
    zapped while blocked on its zappee.
*/

int
zap(int pid)
{
    proc_struct *p;
    int ret = 0;

    DP(DEBUG2, "'%s' with pid %d is zapping %d\n",
              Current->name, Current->pid, pid);

    if (!IS_IN_KERNEL)
        KERNEL_ERROR("'%s' pid %d is not in kernel mode", Current->name, Current->pid);

    if (Current->pid == pid)
        KERNEL_ERROR("Process '%s' pid %d attempting to zap self",
                        Current->name, pid);

    disableInterrupts();

    p = find_process(ReadyList, pid);
    if (!p)
        p = find_process(WaitList, pid);
    if (!p)
        KERNEL_ERROR("Process %d attempting to zap non-existant %d",
                     Current->pid, pid);
    
    /* Now zapped task knows that it's been zapped */
    p->is_zapped = YES_ZAPPED;

    if (!status(p, QUIT))
    {
        DP(DEBUG2, "%s pid %d blocking until zappee quits\n",
                        Current->name, Current->pid);

        /* Record who we're zapping so zappee can find us to unblock us when
           it quits. */
        Current->zappee = p;
        /* Block until zapped process quits */
        ENABLE_INTERRUPTS;
        ret = block_me(BLOCKED_ZAPPING);

        DP(DEBUG2, "%s pid %d unblocking in zap\n",Current->name, Current->pid);

        /* Don't need zappee info anymore. */
        Current->zappee = NULL;
    } else
    {
        ENABLE_INTERRUPTS;
    }

    return ret;
}

/*!
    If the Current task has been zapped, returns YES_ZAPPED, else returns
    NOT_ZAPPED.
*/

int
is_zapped(void)
{
    if (!IS_IN_KERNEL)
        KERNEL_ERROR("'%s' pid %d is not in kernel mode", Current->name, Current->pid);

    return Current->is_zapped;
}

/*!
    Return pid of current task.
*/

int
getpid(void)
{
    return Current->pid;
}

/*!
    Displays all proccess in process table beginning w/ process at
    ProcTable[0] and working to ProcTable[MAXPROC - 1].  Prints the
    header every TERMINAL_LINES lines.

    Lines are assumed 80 characters wide.

    I prefer to only see used process table entries, rather than the
    whole list.  Therefore, I set the #if to 1.  Setting to 0 will
    display them all.
*/

void
dump_processes(void)
{
    const int TERMINAL_LINES = 50;
    int i = 0;
    int count = 0;
    const char *header =
"  Pid  PPid  Prio            Status             #Kids     CPU (us)      Name  \n--------------------------------------------------------------------------------\n";
    disableInterrupts();

    /* Seems like a kindly thing to do: most accurate accounting. */ 
    Current->execution_time += sys_clock() - Current->timeslice_start;

    for ( ; i < MAXPROC; ++i)
    {
        if ((count % TERMINAL_LINES) == 0)
            console("%s",header);

#if 0
        if (ProcTable[i].pid)
        {
            dump_a_process(ProcTable + i);
            ++count;
        }
#else
        dump_a_process(ProcTable + i);
        ++count;
#endif
    }
    ENABLE_INTERRUPTS;
    console("--------------------------------------------------------------------------------\n");
}


/*!
    Block if block_status > 10 (MIN_BLOCK_CODE)

    Process must be at the front of its queue if it's current.
*/

int
block_me(int block_status)
{
    DP(DEBUG, "'%s' pid %d is blocking with code %d\n", Current->name,
              Current->pid, block_status);

    if (block_status < MIN_BLOCK_CODE)
        KERNEL_ERROR("'%s' pid %d invalid block code of %d",
                        Current->name, Current->pid, block_status);

    disableInterrupts();

    /* Set process status */
    Current->execution_time += sys_clock() - Current->timeslice_start;
    code(Current) = block_status;
    set_status(Current, BLOCKED);

    /* Add to end of list of waiting processes */
    add_to_waitlist(Current);

    /* Stop executing here until unblocked. */
    ENABLE_INTERRUPTS;
    DP(DEBUG, "'%s' pid %d is blocking\n", Current->name, Current->pid);
    dispatcher();

    DP(DEBUG2, "'%s' pid %d is unblocking\n", Current->name, Current->pid);

    if (is_zapped())
    {
        DP(DEBUG2, "'%s' pid %d zapped while blocking\n",
                        Current->name, Current->pid);
        return -EZAPPED;
    }
    return 0;
}

/*!
    Unblock the process with pid 'pid' if it
    (a) process with pid 'pid' exists
    (b) isn't Current (itself)
    (c) is blocked
    (d) blocked code is > MIN_BLOCK_CODE

    Additionally, can't unblock processes that are blocked on a join
    or by zapping.
*/

int
unblock_proc(int pid)
{
    disableInterrupts();
    proc_struct *p = get_proc_ptr(WaitList, pid);

    DP(DEBUG2, "'%s' pid %d is unblocking pid %d\n", Current->name,
                    Current->pid, pid);

    if (   (p == NULL)                          /* No process with pid 'pid' */
        || (p == Current)                       /* Can't unblock self */
        || (!status(p, BLOCKED))                /* not blocked */
        || (code(p) <= MIN_BLOCK_CODE))         /* blocked on status < 10 */
    {
        ENABLE_INTERRUPTS;
        return -EBADPID;
    }

    /* We know the task is blocked or it would have failed above */

    /* XXX: from the instructions, it seems like I should just make
     * BLOCKED_JOIN && BLOCKED_ZAPPING < MIN_BLOCK_CODE, but I was wary
     * of any changes */
    if ((code(p) == BLOCKED_JOIN) || (code(p) == BLOCKED_ZAPPING))
    {
        DP(DEBUG3, "Can't unblocked joined or zapping tasks\n");
        ENABLE_INTERRUPTS;
        return -EBADPID;
    }

    if (is_zapped())
    {
        DP(DEBUG2, "'%s' pid %d was zapped while unblocking %d\n",
                        Current->name, Current->pid, pid);
        ENABLE_INTERRUPTS;
        return -EZAPPED;
    }

    /* Must now remove task from WaitList and add to tail of ReadyList */

    p = remove_from_waitlist(p);
    if (!p)
        KERNEL_ERROR("couldn't remove process %d from waitlist",pid);

    code(p) = CLEAR_FLAGS;
    set_status(p, READY);
    add_to_readylist(p);

    DP(DEBUG,"Process '%s' pid %d priority %d added to readylist in unblock_proc\n",
                    p->name, p->pid, p->priority);

    ENABLE_INTERRUPTS;
    /* From the instructions.  "The dispatcher will be called as a
     * side-effect of this function." */
    dispatcher();

    return 0;
}

/*!
    Return time (in microseconds) when current process started running.
*/

int
read_cur_start_time(void)
{
    return Current->timeslice_start;
}

/*!
    If Current process has exhausted its timeslice, call the
    dispatcher.  Convert execution time to milliseconds for this.

    A timeslice is supposed to be 80ms, and CLOCK_MS is 20ms
    (usloss.h), thus the magical number 4.
*/

void
time_slice(void)
{
    #define TIME_SLICE (4 * CLOCK_MS)

    if ( US_TO_MS(sys_clock() - Current->timeslice_start) > TIME_SLICE)
    {
        DP(DEBUG, "%s pid %d: timeslice over\n", Current->name, Current->pid);
        dispatcher();
    }
    DP(DEBUG4, "%s pid %d: tick\n", Current->name, Current->pid);
}


/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes.  The process with the highest
             priority (the first on the ready list) is scheduled to
             run.  The old process is swapped out and the new process
             swapped in.
   Parameters - none
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void
dispatcher(void)
{
    proc_struct *next_process;
    proc_struct *p;
        
    disableInterrupts();


    /* Blocked, etc. tasks don't go on the readylist */
    if (Current && status(Current, RUNNING))
    {
        DP(DEBUG,"'%s' pid %d priority %d was running, now in dispatcher\n",
                 Current->name, Current->priority, Current->pid);
        set_status(Current, READY);
        add_to_readylist(Current);
    }

    next_process = get_from_readylist(HIGHEST_PRIORITY);
    remove_from_readylist(next_process);

    /* I don't understand this USELESS library junk.  This eventually
     * got things working. Bootiful, aitn't it? */
    if (!Current && (next_process->start_func == sentinel))
    {
        /* Leave interrupts disabled or suffer the wrath of the
         * clock_handler irritation bug! */
        add_to_readylist(next_process);
        return;
    }
    if (!Current && (next_process->start_func == start1))
    {
        /* startup1 during 1st call of dispatcher */
        Current = next_process;
        Current->timeslice_start = sys_clock();
        set_status(Current, RUNNING);
        p1_switch(Current->pid, next_process->pid);
        ENABLE_INTERRUPTS;
        context_switch( NULL, &(Current->state));
    } else if (!Current)
        KERNEL_ERROR("NULL Current pointer in dispatcher");

    DP(DEBUG, "Expiring process is '%s' pid %d\n", Current->name, Current->pid);

    if (Current->pid == next_process->pid)
    {
        DP(DEBUG, "dispatcher says reexecute same task\n");

        /* Increment execution time accounting: values in usecs */
        Current->execution_time += sys_clock() - Current->timeslice_start;
        Current->timeslice_start = sys_clock();
        set_status(Current, RUNNING);
        ENABLE_INTERRUPTS;
    } else
    {
        /* Increment execution time accounting */
        Current->execution_time += sys_clock() - Current->timeslice_start;

        /* Apparently I must set Current myself? */
        p = Current;
        Current = next_process;
        Current->timeslice_start = sys_clock();
        set_status(Current, RUNNING);

        DP(DEBUG, "SWITCH: from '%s' pid %d priority %d to '%s' pid %d priority %d \n", p->name, p->pid, p->priority, Current->name, Current->pid, Current->priority);

        p1_switch(Current->pid, next_process->pid);
        ENABLE_INTERRUPTS;
        context_switch( &(p->state), &(Current->state));
    }
}

/*!
    Return how much time the process has executed in milliseconds.
*/

int
readtime(void)
{
    return US_TO_MS(Current->execution_time);
}

