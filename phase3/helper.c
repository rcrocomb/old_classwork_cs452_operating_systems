/*!
    Author: Robert Crocombe
    Class: CS452 Spring 05
    Assignment: Phase 3
*/

#include "helper.h"
#include "utility.h"

#include <usloss.h>
#include <string.h>
#include <usyscall.h>
#include <libuser.h>

#include <stdlib.h>

/******************************************************************************/
/* Globals                                                                    */
/******************************************************************************/

extern semaphore_t semaphore_table[];
extern proc_struct_t process_table[];
extern void (*sys_vec[])(sysargs *args);

/******************************************************************************/
/* Macros and Constants                                                       */
/******************************************************************************/

/* for private mailbox (not semaphores) */
#define CURRENT_MBOX process_table[CURRENT].box_ID

/* Handy for debugging */
#define CURRENT_NAME process_table[CURRENT].name

#define PROCESS_MBOX(a) process_table[GET_SLOT(a)].box_ID

/* Treat a pointer value (or whatever) as an integer */
#define INT_ME(a) ((int)(a))

/*
    Checks that are standard to each syscall function.  Note that
    check 4 only works because I call all my sysargs *s 'args'.  You
    could change this by supplying a 'c' parameter, I guess.

    I've added a test that Patrick seems to want (from page 6 of the
    hints and tips): that the various routines are being pointed to by
    the appropriate entry in the system call vector.  Seems a little
    odd to me, but WTF, yo.

    1st: check to see if in kernel mode.

    2nd: check that the function 'b' is the correct function given the
         syscall value in 'a'. 'b' is basically __func__, but not as a
        string.  Why oh why couldn't there be a de-stringification
        operator?

    3rd: check that the syscall argument structure pointer is not NULL.

    4th: check to see if argument 'a' (a syscall number) is correctly
         matched to the value passed in via the sysarg struct member 'number'.

    I like the stringification operator.
*/
#define STANDARD_CHECKS(a, b) do { \
                        KERNEL_MODE_CHECK; \
                        if (sys_vec[a] != (b)) \
                            KERNEL_ERROR("'%s' not the '%s' handler?", __func__, #a); \
                        if (!args) \
                            KERNEL_ERROR("NULL sysargs"); \
                        if (args->number != (a)) \
                           KERNEL_ERROR("'number' argument is not '%s'", #a); \
                    } while (0)

/*
    I actually think this feels/looks more natural, but the C standard
    apparently disagrees. :(

    #define INT_TO_POINTER(a,b) (int) a = b

    Anyway, given a pointer 'a' and an integer 'b', assigns the
    literal value of 'b' into 'a' by treating it as a pointer to void.
*/
#define INT_TO_POINTER(a,b) a = (void *)b
#define ZERO_POINTER(a) a = NULL

/* Mnemonics for IDs for empty mailboxes and process table entries */
#define EMPTY_BOX -1
#define EMPTY_PID -1

/* Various error values (from specification). */
#define EBADARGS -1
#define ENOSEMS -1
#define EBADSEM -1
#define ESINGLEPARENT -1
#define EZAPPED -1
#define ETERMINATE -1

#define ENOKIDS -2

/******************************************************************************/
/* Prototypes for internal functions                                          */
/******************************************************************************/

/* These are the syscall vector'd functions */
static void spawn(sysargs *args);
static void wait(sysargs *args);
static void terminate(sysargs *args);
static void sem_create(sysargs *args);
static void sem_down(sysargs *args);
static void sem_up(sysargs *args);
static void sem_free(sysargs *args);
static void get_time_of_day(sysargs *args);
static void CPU_time(sysargs *args);
static void get_pid(sysargs *args);

/* These do the real work of the above syscalls */
static void terminate_real(int quit_code);
static void terminate_recurse(proc_struct_t *p);
static int sem_create_real(int count);
static int sem_down_real(int sem_ID);
static int sem_up_real(int sem_ID);
static int sem_free_real(int sem_ID);

static int spawn_launch(char *arg);

/* Lending a hand */
static int get_next_sem_ID(void);
static void add_to_child_list(proc_struct_t *parent, proc_struct_t *kid);
static proc_struct_t *remove_from_child_list(proc_struct_t *parent,
                                             proc_struct_t *kid);
static void break_list(proc_struct_t *parent,
                       proc_struct_t *p,
                       proc_struct_t *previous);

static int count_kids(proc_struct_t *p);

static void initialize_a_process_entry(proc_struct_t *p);
static void initialize_a_semaphore_entry(semaphore_t *s);

static void nullsys3(sysargs *args);

/******************************************************************************/
/* Function Definitions                                                       */
/******************************************************************************/

/*!
    Creates a new, user-level task using the arguments in 'args'.

    Sysargs upon entering:

    arg1: function pointer
    arg2: argument pointer
    arg3: stack_size
    arg4: priority
    arg5: task name

    Sysargs when returning:

    arg1: pid
    arg4: return code

    Function checks arguments, then formats them in a nicer manner for
    spawn_real, which does the actual work.

    All error conditions are return in args->arg4.  If there are no errors,
    then args->arg1 is set to the pid of the new child.  args->arg4
    will be 0 in this case, so that you know to check arg1.

*/

void
spawn(sysargs *args)
{
    int priority;
    int ret;

    STANDARD_CHECKS(SYS_SPAWN, spawn);

    /* function pointer: can't be NULL: arg1 */
    if (!args->arg1)
    {
        DP(DEBUG3, "NULL function pointer\n");
        goto out;
    }

    /* arg can be NULL if it wants: arg2 */

    /* stack: must be at least USLOSS_MIN_STACK: arg3 */
    if (INT_ME(args->arg3) < USLOSS_MIN_STACK)
    {
        DP(DEBUG3, "Stack too small at %d bytes\n", args->arg3);
        goto out;
    }

    /* priority needs to be within legal range: arg4 */
    priority = INT_ME(args->arg4);
    if ((priority < HIGHEST_PRIORITY) || (priority > LOWEST_PRIORITY))
    {
        DP(DEBUG3, "Invalid priority of %d\n", priority);
        goto out;
    }

    /* Name cannot be NULL: arg5 */
    if (!args->arg5)
    {
        DP(DEBUG3, "Null pointer for task name\n");
        goto out;
    }

    /* If the spawn was successful, put the child's pid in the proper
       spot in the sysargs struct, else put the error code into its
       (different) spot. */
    ret = spawn_real(args->arg5, args->arg1, args->arg2,
                     INT_ME(args->arg3), priority);
    if (ret >= 0)
    {
        INT_TO_POINTER(args->arg1, ret);         /* good */
        ZERO_POINTER(args->arg4);
    } else
    {
        /* process could not be created */
        INT_TO_POINTER(args->arg1, ret);         /* bad */
        INT_TO_POINTER(args->arg4, EBADARGS);
    }

out:
    return;
}

/*!
    Wait for a child to terminate.

    If okay, then arg4 == 0, else arg4 == -1.

    If arg4 == 0, then arg1 == terminating child's pid and arg2 == quit code.
*/

void
wait(sysargs *args)
{
    int ret;
    int status;

    STANDARD_CHECKS(SYS_WAIT, wait);

    /* ret sould be pid of the child that has quit */
    ret = wait_real(&status);
    if (ret >= 0)
    {
        INT_TO_POINTER(args->arg1, ret);     /* success */
        INT_TO_POINTER(args->arg2, status);
        ZERO_POINTER(args->arg4);
    }
    else
        INT_TO_POINTER(args->arg4, ESINGLEPARENT);
}

/*!
    Kill all descendents (all children, all children of children,
    etc.), then quit with termination code in args->arg1.
*/

void
terminate(sysargs *args)
{
    STANDARD_CHECKS(SYS_TERMINATE, terminate);

    DP(DEBUG5, "beginning for process %d\n", getpid());
    terminate_real(INT_ME(args->arg1));
    DP(DEBUG5, "finished for %d\n", getpid());
}

/*!
    Create a semaphore with count == args->arg1.

    If successful, then args->arg4 == 0, else args->arg4 == -1.

    If successful, then args->arg1 == semaphore handle to use in SemP
    and SemV operations ('down' and 'up' for non-Dutch speakers).
*/

void
sem_create(sysargs *args)
{
    int count;
    int sem_ID;

    STANDARD_CHECKS(SYS_SEMCREATE, sem_create);

    /* assume failure */
    INT_TO_POINTER(args->arg4, EBADARGS);

    count = (int) args->arg1;
    if (count < 0)
    {
        DP(DEBUG3, "Invalid initial sem value of %d\n", count);
        goto out;
    }

    sem_ID = sem_create_real(count);
    if (sem_ID >= 0)
    {
        /* be pleasantly surprised by success. */
        INT_TO_POINTER(args->arg1, sem_ID);
        ZERO_POINTER(args->arg4);
    }

out:
    return;
}

/*!
    Acquire the semaphore whose ID is in args->arg1.  Block if
    semaphore is all used up.

    args->arg4 will be set to -1 if the semaphore handle is invalid
    (presumably what will lead to the errors in the mailbox stuff (?) ),
    or 0 otherwise.
*/

void
sem_down(sysargs *args)
{
    int sem_ID;
    int ret;

    STANDARD_CHECKS(SYS_SEMP, sem_down);

    sem_ID = INT_ME(args->arg1);
    if ((sem_ID < 0) || (sem_ID >= MAXSEMS))
        INT_TO_POINTER(args->arg4, EBADSEM);
    else
    {
        /* 0 on success, or -1 if bad things happened */
        ret = sem_down_real(sem_ID);
        INT_TO_POINTER(args->arg4, ret);
    }
}

/*!
    Posts to the semaphore identified by the handle in args->arg1.

    args->arg4 is set to 0 on success, or -1 on error.
*/

void
sem_up(sysargs *args)
{
    int sem_ID;
    int ret;

    STANDARD_CHECKS(SYS_SEMV, sem_up);

    sem_ID = INT_ME(args->arg1);
    if ((sem_ID < 0) || (sem_ID > MAXSEMS))
        INT_TO_POINTER(args->arg4, EBADSEM);
    else
    {
        /* 0 on success, or -1 if bad things happened */
        ret = sem_up_real(sem_ID);
        INT_TO_POINTER(args->arg4, ret);
    }
}

/*!
    Get rid of a semaphore (assuming valid ID).  If there are
    processes blocked on this semaphore, they are all terminated.

    This routine actually just checks the validity of arguments, then
    calls sem_free_real to do the actual work.

    Returns -1 if the semaphore ID is illegal
    Returns 1 if there were processes blocked on the semaphore
    Returns 0 if neither of the above is true.
*/

void
sem_free(sysargs *args)
{
    int sem_ID;
    int ret;

    STANDARD_CHECKS(SYS_SEMFREE, sem_free);

    /* Make sure semaphore ID is within the legal range */
    sem_ID = INT_ME(args->arg1);
    if ((sem_ID < 0) || (sem_ID >= MAXSEMS))
    {
        DP(DEBUG3, "Illegal semaphore ID of %d\n", sem_ID);
        INT_TO_POINTER(args->arg4, EBADSEM);
    } else
    {
        ret = sem_free_real(sem_ID);
        DP(DEBUG3, "Return from sem_free_real is %d\n", ret);
        INT_TO_POINTER(args->arg4, ret);
    }
}

/*!
    This returns the "time of day" (?), or as much as the OS knows
    about it: sys_clock returns the time since boot.

    This value is in microseconds.
*/

void
get_time_of_day(sysargs *args)
{
    int clock;

    STANDARD_CHECKS(SYS_GETTIMEOFDAY, get_time_of_day);

    clock = sys_clock();
    INT_TO_POINTER(args->arg1, clock);
}

/*!
    Return the processor time used by this process (in milliseconds).
*/

void
CPU_time(sysargs *args)
{
    int time;

    STANDARD_CHECKS(SYS_CPUTIME, CPU_time);

    time = readtime();
    INT_TO_POINTER(args->arg1, time);
}

/*!
    Return the process ID of the current process.
*/

void
get_pid(sysargs *args)
{
    int pid;

    STANDARD_CHECKS(SYS_GETPID, get_pid);
    pid = getpid();
    INT_TO_POINTER(args->arg1, pid);
}

/******************************************************************************/
/* "Real" functions -- kernel mode functions that actually do work            */
/******************************************************************************/

/*!
    Assume all arguments checked, modes are correct, etc.
*/

int
spawn_real(char *name, func_p f, char *arg, int stack_size, int priority)
{
    int pid, ret;

    DP(DEBUG5, "pid %d, calling fork1 for child '%s' prio %d \n",
               getpid(), name, priority);

    /* The only error code that should be returnable by fork1 is -1
       for when we are out of process table entries.  All other possible
       error conditions should have been caught in spawn() already.  */
    pid = fork1(name, spawn_launch, arg, stack_size, priority);
    if (pid >= 0)
    {
        DP(DEBUG3, "%d spawned '%s' with pid %d\n", getpid(), name, pid);

        /* Child is blocked here.  Either (a) they were higher
           priority and blocked on their mailbox when dispatched from within
           fork1, or (b) they were lower priority and we are still running and
           they are on the ready list not doing anything.  Yay, lack of SMP. */

        process_table[GET_SLOT(pid)].pid = pid;
        process_table[GET_SLOT(pid)].func = f;
        process_table[GET_SLOT(pid)].ppid = getpid();

        /* This is nice for debugging.  Otherwise, not needed (?). */
        strncpy(process_table[GET_SLOT(pid)].name, name, MAXNAME - 1);

        /* Add child to list of our children. Yay no SMP some more! */
        add_to_child_list(&process_table[CURRENT], &process_table[GET_SLOT(pid)]);

        /* now that I know the child's pid, I can send a message
           to its private mailbox to unblock it.  Once unblocked, it
           can access its process table entry (to which I've written
           the function it needs to execute). Because the mailbox is
           of sufficient size to hold this message, this process will
           not block when doing this send.  What is sent doesn't
           really matter: I send the process its own pid.  */
        ret = MboxSend(process_table[GET_SLOT(pid)].box_ID, &pid, sizeof(pid));
        if (ret < 0)
            KERNEL_ERROR("pid %d failed in send to child process %d's mbox\n",
                         getpid(), pid);
    }
    else
        DP(DEBUG, "%d: spawn failed creating child '%s'\n", getpid(), name);

    return pid;
}

/*!
    Returns -1 if there were no kids, or else the pid of the child
    that was waited for (joined).  The quit code of the child is in status.
*/

int
wait_real(int *status)
{
    int ret = join(status);
    if (ret == ENOKIDS)
        ret = ESINGLEPARENT;
    else if (ret == EZAPPED)
    {
        DP(DEBUG,"%d zapped while waiting.\n", getpid());
        terminate_real(EZAPPED);
    }

    return ret;
}

/*!
    Business end of the terminate routines.
*/

void
terminate_real(int quit_code)
{
    proc_struct_t *p = &process_table[CURRENT];

    DP(DEBUG5, "beginning for %d:%d: quit code is %d\n",
               getpid(), p->pid, quit_code);

    /* terminate all descendents */
    terminate_recurse(p->kids_front);

    /* remove from list of children of parent so that sem_free doesn't
       cause an explosion. */
    remove_from_child_list(&process_table[GET_SLOT(p->ppid)], p);

    DP(DEBUG5,"calling quit with quit code %d\n", quit_code);

    /* Kill self. */
    quit(quit_code);

    DP(DEBUG5,"after quit?\n");
}

/*!
    Depth-first zapping for 'p', p's siblings, and all p's descendents.
*/

void
terminate_recurse(proc_struct_t *p)
{
    int ret, box_ID;

    if (!p)
    {
        DP(DEBUG, "process %d: no kids\n", getpid());
        return;
    }

    /* zap "self" (not running process), but process 'p' */
    zap(p->pid);

    /* Clean mailbox */
    ret = MboxRelease(PROCESS_MBOX(p->pid));
    if (ret < 0)
        KERNEL_ERROR("Error releasing mbox %d: %d", PROCESS_MBOX(p->pid), ret);

    /* Start fresh with new one */
    box_ID = MboxCreate(1, sizeof(int));
    if (box_ID < 0)
        KERNEL_ERROR("Error creating private mailbox at %d\n",
                     p - process_table);
    PROCESS_MBOX(p->pid) = box_ID;

    /* Own children (down) */
    if (p->kids_front)
        terminate_recurse(p->kids_front);

    /* Across siblings */
    if (p->next)
        terminate_recurse(p->next);

    /* Done zapping siblings and kids */

    DP(DEBUG5,"recursion done\n");
}

/*!
    Our semaphore will consist of a mailbox with a number of slots
    equal to the starting value of the semaphore.

    Returns ID of semaphore if creation was successful, or -1 if it was not.

    The sem_ID is the index into the semaphore table at which we
    record a mailbox ID.  Mailboxes are the mechanism by which semaphores
    are implemented.
*/

int
sem_create_real(int count)
{
    int sem_ID = get_next_sem_ID();
    int box_ID;

    if (sem_ID < 0)
    {
        DP(DEBUG, "No more semaphores possible: %d\n", sem_ID);
        return sem_ID;
    }

    box_ID = MboxCreate(count, 0);
    semaphore_table[sem_ID].box_ID = box_ID;
    semaphore_table[sem_ID].count = count;

    DP(DEBUG3, "Process %d created a sem with count == '%d': "
               "sem ID == '%d' using mailbox %d\n",
              getpid(), count, sem_ID, box_ID);

    return sem_ID;
}

/*!
    I need the decrement to happen before we [potentially] block so
    that I can see that there are tasks blocked on a semaphore to return
    the correct code in sem_free (count will be < 0 if processes are
    blocked on sem_ID).
*/

int
sem_down_real(int sem_ID)
{
    int ret;
    semaphore_t *s = &semaphore_table[sem_ID];

    DP(DEBUG3, "Process %d acquiring semaphore with ID '%d': mailbox is %d\n",
              getpid(), sem_ID, s->box_ID);

    --(s->count);
    if (s->count < 0)
        DP(DEBUG3, "Count is %d on %d (%d) for pid %d should block\n",
                  s->count, sem_ID, s->box_ID, getpid());


    /* Will block if semaphore is fully acquired (count now < 0): all
       mailbox slots full */
    ret = MboxSend(s->box_ID, NULL, 0);
    if (ret < 0)
    {
        DP(DEBUG, "sem ID %d: error when sending on mailbox %d: %d\n",
                  sem_ID, s->box_ID, ret);
        terminate_real(1); /* test16 indicates should be '1', but why? */
    }

    DP(DEBUG3, "Semaphore %d unblocked for process %d\n", sem_ID, getpid());

    return ret;
}

/*!
    Increment semaphore slots/spots available, but cap the value at
    the maximum for the semaphore: no using a bunch of V operations to add
    additional capacity to semaphore.
*/

int
sem_up_real(int sem_ID)
{
    int ret;
    semaphore_t *s = &semaphore_table[sem_ID];

    DP(DEBUG, "Process %d: posting sem %d\n", getpid(),sem_ID);

    ++(s->count);
    /* Cannot release a semaphore more than max_count */
    if (s->count > s->max_count)
        s->count = s->max_count;

    /* Should never ever ever ever block. */
    ret = MboxReceive(semaphore_table[sem_ID].box_ID, NULL, 0);
    if (ret < 0)
    {
        DP(DEBUG, "sem ID %d error when receiving on mailbox %d: %d\n",
                  sem_ID, semaphore_table[sem_ID].box_ID, ret);
        ret = EBADSEM;
    }

    DP(DEBUG, "complete for %d semaphore ID %d\n", getpid(), sem_ID);
    return ret;
}

/*!
    Assumes that arguments were checked.

    The sem_ID is an index into the sempahore_table where info about
    the semaphore is stored.

    Returns 1 if there were processes blocked on the semaphore, or 0
    if the semaphore was released successfully.
*/

int
sem_free_real(int sem_ID)
{
    int ret;

    /* Determine if tasks are blocked on sem_ID */
    int has_blockees = semaphore_table[sem_ID].count < 0;

    DP(DEBUG3, "Trying to free semaphore with ID %d: box is %d\n",
               sem_ID, semaphore_table[sem_ID].box_ID);

    /* Now get rid of all processes blocked on the mailbox */
    ret = MboxRelease(semaphore_table[sem_ID].box_ID);
    if (ret < 0)
        KERNEL_ERROR("MboxRelease for 'semaphore ID' %d failed: %d",
                     sem_ID, ret);

    initialize_a_semaphore_entry(&semaphore_table[sem_ID]);

    return has_blockees;
}

/******************************************************************************/
/* Initialization Routines                                                    */
/******************************************************************************/

/*!
    Initialize process table entries, and create a private mailbox for
    each process table entry to be used for synchronization use when
    spawning.
*/

void
initialize_a_process_entry(proc_struct_t *p)
{
    int box_ID;

    p->pid = EMPTY_PID;
    p->ppid = EMPTY_PID;
    p->kids_front = NULL;
    p->kids_back = NULL;
    p->next = NULL;
    p->name[0] = '\0';

    box_ID = MboxCreate(1, sizeof(int));
    if (box_ID < 0)
        KERNEL_ERROR("Error creating private mailbox at %d\n",
                     p - process_table);
    p->box_ID = box_ID;
}

/*!
    Ensure that process table entries are initialized correctly.
*/

void
initialize_process_table(void)
{
    int i = 0;

    DP(DEBUG5, "Initializing process table\n");
    for ( ; i < MAXPROC; ++i)
        initialize_a_process_entry(process_table + i);
}

/*!
    Clear semaphore table entry to recognizable values.
*/

void
initialize_a_semaphore_entry(semaphore_t *s)
{
    s->box_ID = EMPTY_BOX;
    s->count = 0;
    s->max_count = 0;
}

/*!
    Make sure that the semaphore table entries are set to values so
    that we can recognize empty entries.
*/

void
initialize_semaphore_table(void)
{
    int i = 0;
    DP(DEBUG5, "Initializing semaphore table\n");
    for ( ; i < MAXSEMS; ++i)
        initialize_a_semaphore_entry(semaphore_table + i);
}

/*!
    Invalid system calls go to nullsys3, which terminates the
    offending process.

    Valid system calls go to associated handlers.
*/

void
initialize_sys_vec(void)
{
    int i = 0;

    for ( ; i < MAXSYSCALLS; ++i)
       sys_vec[i] = nullsys3;

    sys_vec[SYS_SPAWN]          = spawn;
    sys_vec[SYS_WAIT]           = wait;
    sys_vec[SYS_TERMINATE]      = terminate;
    sys_vec[SYS_SEMCREATE]      = sem_create;
    sys_vec[SYS_SEMP]           = sem_down;
    sys_vec[SYS_SEMV]           = sem_up;
    sys_vec[SYS_SEMFREE]        = sem_free;
    sys_vec[SYS_GETTIMEOFDAY]   = get_time_of_day;
    sys_vec[SYS_CPUTIME]        = CPU_time;
    sys_vec[SYS_GETPID]         = get_pid;
}

/******************************************************************************/
/* Internal routines                                                          */
/******************************************************************************/

/*!
    Wrapped around the user's function so that we can set the mode to
    user-mode, and additionally to make sure that the task terminates
    properly if it should return to this routine (returned from its
    function).

    Initially retrieves parent's pid from its private mailbox.  Parent
    sent the message to us after the fork1 completed.  If we were higher
    priority than the parent and ran first, then we block on the receive
    until parent does run.  Elsewise, we were lower priority and the
    receive doesn't block because the parent send the message already.  In
    neither case does the parent block: only priority matters as to
    which way things happen.
*/

int
spawn_launch(char *arg)
{
    int pid, ret;
    func_p f;

    /*
        If zapped before even launched, then recognize that here and return.

        This can happen when high priority tasks create and then
        destroy children processes before they run.  I'm not sure if doing
        this here is a kludge cuz I screwed up something somewheres else and
        processes should be noticing they're zapped elsewhere or what.  Heck,
        I pass the testcases now, though...
    */
    if (is_zapped())
        return EZAPPED;

    ret = MboxReceive(process_table[CURRENT].box_ID, &pid, sizeof(pid));
    if (ret < 0)
        KERNEL_ERROR("MboxReceive failed for '%s' pid %d on box %d\n",
                     CURRENT_NAME, getpid(), CURRENT_MBOX);
#if 1
    if (pid != getpid())
        KERNEL_ERROR("Ouchies in pid %d '%s': "
                     "pid doesn't match pid from message: %d\n",
                     getpid(), CURRENT_NAME, pid);
#endif

    /* Now get our needed stuff from own process table entry */
    f = process_table[CURRENT].func;
    if (!f)
        KERNEL_ERROR("'%s' pid %d: Null func", CURRENT_NAME, getpid());

    DP(DEBUG5, "'%s' pid %d:%d parent %d arg '%s'\n",
               CURRENT_NAME, getpid(), pid, process_table[CURRENT].ppid,
               (arg ? arg : "NULL"));

    DP(DEBUG3, "%d Going to user mode function\n", getpid());
    go_user_mode();

    /* Begin executing function with supplied argument in user mode */
    ret = f(arg);

    /* Returned from routine.  Terminate. */
    Terminate(ret);

    DP(DEBUG5,"terminating");
    return 0;
}

/*!
    Returns the index within the semaphore table that should next be used.
*/

int
get_next_sem_ID(void)
{
    int i = 0;

    for ( ; i < MAXSEMS; ++i)
    {
        if (semaphore_table[i].box_ID == EMPTY_BOX)
            break;
    }

    if (i == MAXSEMS)
        return ENOSEMS;
    return i;
}

/*!
    Add process 'kid' as a child process of process 'parent'.

    Any other children that 'parent' already has will become siblings
    of 'kid': 'kid' goes on the end of any existing queue of children.
*/

void
add_to_child_list(proc_struct_t *parent, proc_struct_t *kid)
{
    DP(DEBUG5, "beginning: adding %d as child of %d:%d\n",
               kid->pid, kid->ppid, parent->pid);

    if (!parent)
        KERNEL_ERROR("NULL parent");

    if (!kid)
        KERNEL_ERROR("NULL kid");

    if (!parent->kids_back) /* 1st to be enqueued */
    {
        parent->kids_front = kid;
        parent->kids_back = kid;
        kid->next = NULL;
    } else                  /* Add to end of queue */
    {
        parent->kids_back->next = kid;
        parent->kids_back = kid;
        kid->next = NULL;
    }

    DP(DEBUG5, "finished: process %d now has %d kids\n",
               parent->pid, count_kids(parent));
}

/*!
    Removes process 'kid' from list of children of parent process 'parent'.

    Returns pointer to this child process (is now kernel error if
    can't find it).
*/

proc_struct_t *
remove_from_child_list(proc_struct_t *parent, proc_struct_t *kid)
{
    proc_struct_t *p = NULL;
    proc_struct_t *previous = NULL;

    if (!parent)
        KERNEL_ERROR("parent pointer is NULL\n");

    if (!kid)
        KERNEL_ERROR("kid pointer is NULL\n");

    DP(DEBUG5, "starting: removing child %d from parent %d:%d\n",
               kid->pid, kid->ppid, parent->pid);

    /* Find proc and element previous to it in queue */
    if (parent->kids_front)
    {
        previous = p = parent->kids_front;
        while (p && (p != kid))
        {
            previous = p;
            p = p->next;
        }
    }

    /* Remove element from list */
    if (p)
    {
        break_list(parent, p, previous);
        DP(DEBUG5, "Process %d now has %d kids\n",
                    parent->pid, count_kids(parent));
    } else
        KERNEL_ERROR("Couldn't find %d in child list of %d\n",
                     kid->pid, parent->pid);

    return p;
}

/*!
    Does the actual list link magic for removing an element from a list.
*/

void
break_list(proc_struct_t *parent, proc_struct_t *p, proc_struct_t *previous)
{
    DP(DEBUG5, "Breaking list for %d: removing %d\n", parent->pid, p->pid);

    /* Found node and previous node: rejoin list around 'p' */
    if (p == previous)
    {
        DP(DEBUG5,"first\n");
        /* First in queue */
        parent->kids_front = parent->kids_front->next;
        /* Only in queue? */
        if (parent->kids_back == p)
            parent->kids_back = NULL;
    } else
    {
        DP(DEBUG5,"not first\n");
        /* Not first (and therefore not only) */
        previous->next = p->next;
        /* Last? */
        if (p == parent->kids_back)
            parent->kids_back = previous;
    }
}

/*!
    Counts how many children process 'p' has.
*/

int
count_kids(proc_struct_t *p)
{
    proc_struct_t *t = p->kids_front;
    int count = 0;

    if (t)
    {
        do {
            ++count;
            t = t->next;
        } while (t);
    }
    return count;
}

/*!
    Function to be called for an index outside of those assigned to
    valid handlers.

    Terminates the calling routine with the quit code ETERMINATE.
*/

void
nullsys3(sysargs *args)
{
    DP(DEBUG, "Calling\n");
    terminate_real(ETERMINATE);
}

