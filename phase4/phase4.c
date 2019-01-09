/*!
    Author: Robert Crocombe
    Class: CS452 Spring 05
    Assignment: Phase 4

    The driver-kicker-offer piece.  start3() is the mammer-jammer, and
    forks all the various subsidiary driver processes (there are
    many), forks start4 to do work, and then cleans up after it
    terminates.

    Everything aside from start3, then, are support routines for its work.
*/

#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <usyscall.h>

#include "utility.h"
#include "syscall.h"
#include "drivers.h"
#include "types.h"

/******************************************************************************/
/* Macros                                                                     */
/******************************************************************************/

/* Unit names can be this long based on MAX_UNITS */
#define UNIT_STRING_LENGTH (MAX_UNITS / 10)
#define DEVICE_DRIVER_PRIO 2
#define LINES_TO_BUFFER 10

#define START4_PRIO 3

/******************************************************************************/
/* Global variables                                                           */
/******************************************************************************/

int debugflag4 = NO_DEBUG;

proc_table_entry process_table[MAXPROC];
int mailbox_table[MAXMBOX];

/* making them contiguous makes contains() routine easy */
int disk_pids[DISK_UNITS];
int term_driver_pids[TERM_UNITS];
int term_receiver_pids[TERM_UNITS];
int term_transmitter_pids[TERM_UNITS];

/* other, non-pid device info */
clock_info_t clock_info;
disk_info_t disk_info[DISK_UNITS];
term_info_t term_info[TERM_UNITS];

/******************************************************************************/
/* External prototypes                                                        */
/******************************************************************************/

extern int spawn_real(char *name, int (*)(char *), char *, int, int);
extern int wait_real(int *);

/******************************************************************************/
/* Internal prototypes                                                        */
/******************************************************************************/

static void fork_clocks(void);
static void fork_disks(void);
static void fork_terms(void);

static int contains(int x, int array[], int size);
static void all_dead(void);
static void do_joins(void);
static void wake_up(void);


static void initialize_process_entry(int index);
static void initialize_process_table(void);
static void initialize_sysvec(void);
static void initialize_clock_data(void);
static void initialize_disk_data(void);
static void initialize_term_data(void);

/******************************************************************************/
/* Definitions                                                                */
/******************************************************************************/

/*!
    First, initialize all data structures to be used in this phase.

    Then, start all the device driver processes.

    Third, create and run start4(), which will create other processes
    to do actual, you know, work and stuff.

    While start4() is running, start3() will wait for it, doing
    nothing.  When start4() ultimately returns, start4() will see to
    the orderly shutdown and clean up of the various tasks it created
    in step 2.
*/

int
start3(char *arg)
{
    int pid, status, ret;

    KERNEL_MODE_CHECK;

    initialize_process_table();
    initialize_clock_data();
    initialize_disk_data();
    initialize_term_data();

    /* syscall vectors for functions requesting services from device drivers */
    initialize_sysvec();

    if (CLOCK_UNITS > MAX_UNITS)
        KERNEL_ERROR("Too many clocks: %d", CLOCK_UNITS);

    if (DISK_UNITS > MAX_UNITS)
        KERNEL_ERROR("Too many disks: %d", DISK_UNITS);

    if (TERM_UNITS > MAX_UNITS)
        KERNEL_ERROR("Too many terminals: %d", TERM_UNITS);

    /* create device driver processes */
    fork_clocks();
    fork_disks();
    fork_terms();

    pid = spawn_real("start4", start4, NULL, 2 * USLOSS_MIN_STACK, START4_PRIO);
    if (pid < 0)
        KERNEL_ERROR("Error spawning start4: %d", pid);

    /* expect it to be shutdown for start4, but mayn't if other
     * processes quit unexpectedly. */
    ret = wait_real(&status);
    if (ret < 0)
        KERNEL_ERROR("Error waiting in start3: %d\n", ret);

    DP(DEBUG4,"start3 complete: joined on %d with status %d\n", ret, status);

    /* Join against all device driver processes */
    do_joins();

    return 0;
}

/******************************************************************************/
/* Internal definitions                                                       */
/******************************************************************************/

/*
    One process per clock (there's only 1).  I have apriori knowledge
    about this, so I'll leave off the loop.
*/

void
fork_clocks(void)
{
    int  ret;
    char proc_name[MAXNAME];
    char unit[UNIT_STRING_LENGTH + 1];

    if (CLOCK_UNITS != 1)
        KERNEL_ERROR("Assumption about clock count violated.  Me == Fux0red!");

    sprintf(proc_name, "%s%d","clock_driver_", 0);
    sprintf(unit, "%d", 0);
    ret =  fork1(proc_name, clock_driver, unit,
                 USLOSS_MIN_STACK, DEVICE_DRIVER_PRIO);
    if (ret < 0)
        KERNEL_ERROR("Creating driver for the clock");
    DP(DEBUG3, "clock process is pid %d\n", ret);
    clock_info.pid = ret;
}

/*
    One process per disk (there're 2)
*/

void
fork_disks(void)
{
    int i, ret;
    char proc_name[MAXNAME];
    char unit[UNIT_STRING_LENGTH + 1];
    for (i = 0; i < DISK_UNITS; ++i)
    {
        sprintf(proc_name, "%s%d","disk_driver_", i);
        sprintf(unit, "%d", i);
        ret = fork1(proc_name, disk_driver, unit,
                    USLOSS_MIN_STACK, DEVICE_DRIVER_PRIO);
        if (ret < 0)
            KERNEL_ERROR("Creating driver for disk %d", i);
        DP(DEBUG3, "disk %d process is %d\n", i, ret);
        disk_pids[i] = ret;
    }
}

/*
    3 processes per terminal (there're 4): total of 12 processes.

    1 process waits on interrupts (terminal_driver)

    1 process waits on a mailbox from terminal_driver: gets characters
    as they arrive from terminal

    1 process waits on a mailbox from TermWrite
*/

void
fork_terms(void)
{
    int i, ret;
    char proc_name[MAXNAME];
    char unit[UNIT_STRING_LENGTH + 1];

    for (i = 0; i < TERM_UNITS; ++i)
    {
        /* enable Rx interrupts, disable Tx interrupts */
        set_term_interrupts(i, RX_ON | TX_OFF);

        /* fork main driver process */
        sprintf(proc_name, "%s%d", "term_driver_", i);
        sprintf(unit, "%d", i);
        ret = fork1(proc_name, terminal_driver, unit,
                    USLOSS_MIN_STACK, DEVICE_DRIVER_PRIO);
        if (ret < 0)
            KERNEL_ERROR("Creating interrupt listener for term %d: %d", i, ret);
        DP(DEBUG3, "term driver %d process is %d\n", i, ret);
        term_driver_pids[i] = ret;

        /* fork reader process */
        sprintf(proc_name, "%s%d","term_rx_", i);
        sprintf(unit, "%d", i);
        ret = fork1(proc_name, terminal_receiver, unit,
                    USLOSS_MIN_STACK, DEVICE_DRIVER_PRIO);
        if (ret < 0)
            KERNEL_ERROR("Creating receiver for term %d: %d", i, ret);
        term_receiver_pids[i] = ret;
        DP(DEBUG3, "term rx %d process is %d\n", i, ret);

        /* fork writer process */
        sprintf(proc_name, "%s%d","term_tx_", i);
        sprintf(unit, "%d", i);
        ret = fork1(proc_name, terminal_transmitter, unit,
                    USLOSS_MIN_STACK, DEVICE_DRIVER_PRIO);
        if (ret < 0)
            KERNEL_ERROR("Creating transmitter for term %d: %d", i, ret);
        term_transmitter_pids[i] = ret;
        DP(DEBUG3, "term tx %d process is %d\n", i, ret);
    }
}

/*!
    If 'x' is within 'array', returns the [1st] index where it was
    found, else returns -1.
*/

int
contains(int x, int array[], int size)
{
    int i = 0;
    for ( ; i < size; ++i)
    {
        DP(DEBUG5, "%d Matching %d against %d\n", i, x, array[i]);
        if (array[i] == x)
            break;
    }

    if (i < size)
        DP(DEBUG5, "Found match @ %d\n", i);
    else
        DP(DEBUG5, "No match for %d in array\n", x);
    return i < size ? i : -1;
}

/*!
    More of a debugging routine, now.  Tells me what processes weren't
    joined by do_join().  I can investigate to see if this is legit or
    not.
*/
void
all_dead(void)
{
    int i;

    if (clock_info.pid != NOT_A_PID)
        DP(DEBUG4,"Clock process %d hasn't quit!\n", clock_info.pid);

    for (i = 0; i < DISK_UNITS; ++i)
    {
        if (disk_pids[i] != NOT_A_PID)
            DP(DEBUG4,"Disk process %d for unit %d hasn't quit!\n",
                           disk_pids[i], i);
    }

    for (i = 0; i < TERM_UNITS; ++i)
    {
        if (term_driver_pids[i] != NOT_A_PID)
            DP(DEBUG4,"Interrupt process %d for term %d hasn't quit!\n",
                           term_driver_pids[i], i);

        if (term_receiver_pids[i] != NOT_A_PID)
            DP(DEBUG4,"Rx process %d for term %d hasn't quit!\n",
                           term_receiver_pids[i], i);

        if (term_transmitter_pids[i] != NOT_A_PID)
            DP(DEBUG4,"Tx process %d for term %d hasn't quit!\n",
                           term_transmitter_pids[i], i);
    }
}

/*!
    At OS shutdown, ensure that all device driver and related
    processes have successfully terminated.

    I think that the sentinel or something else can reap children,
    too, so sometimes I may not get all off them here, and still be
    without children (or maybe it was bugs...).  So try to get them
    all, but if no kids remain, then give up.
*/

void
do_joins(void)
{
    const int TOTAL_DEVICES = CLOCK_UNITS + DISK_UNITS + (3 * TERM_UNITS);

    int pid,
        status,
        ret,
        found_match,
        devices_joined = 0;

    DP(DEBUG4,"Shutting down: stop drivers\n");

    /* wake up device drivers and make them terminate */
    wake_up();

    do
    {
        found_match = 0;
        pid = join(&status);
        if (pid == -ENOKIDS)
        {
            DP(DEBUG3,"No more children remain\n");
            break;
        }

        DP(DEBUG3, "Joined on process %d : %d\n", pid, status);

        if (pid == clock_info.pid)
        {
            clock_info.pid = NOT_A_PID;
            ++devices_joined;
            found_match = 1;
        }

        ret = contains(pid, disk_pids, DISK_UNITS);
        if (ret != -1)
        {
            disk_pids[ret] = NOT_A_PID;
            ++devices_joined;
            found_match = 1;
        }

        /* interrupt listener processes */
        ret = contains(pid, term_driver_pids, TERM_UNITS);
        if (ret != -1)
        {
            term_driver_pids[ret] = NOT_A_PID;
            ++devices_joined;
            found_match = 1;
        }

        /* char Rx processes */
        ret = contains(pid, term_receiver_pids, TERM_UNITS);
        if (ret != -1)
        {
            term_receiver_pids[ret] = NOT_A_PID;
            ++devices_joined;
            found_match = 1;
        }

        /* char Tx processes */
        ret = contains(pid, term_transmitter_pids, TERM_UNITS);
        if (ret != -1)
        {
            term_transmitter_pids[ret] = NOT_A_PID;
            ++devices_joined;
            found_match = 1;
        }

        if (found_match == 0)
            DP(DEBUG, "Joined on non-device process %d?\n", pid);

    } while (devices_joined < TOTAL_DEVICES);

    DP(DEBUG3, "%d devices of %d joined\n", devices_joined, TOTAL_DEVICES);
    all_dead();
}

/*!
    Generates activity that will get the drivers to notice it's time
    for them to Go Away.
*/

void
wake_up(void)
{
    int i, ret;

    DP(DEBUG4,"Zapping clock\n");
    zap(clock_info.pid);
    DP(DEBUG4,"Clock zapped\n");

    /* Sufficient to make them quit */
    for (i = 0; i < DISK_UNITS; ++i)
    {
        DP(DEBUG4, "Sendy wake signal to disk %d\n", i);
        MboxCondSend(disk_info[i].box_ID, 0, 0);
    }

    for (i = 0; i < TERM_UNITS; ++i)
    {
        ret = MboxRelease(term_info[i].rx_box);
        DP(DEBUG4,"Disk %d rx_box %d -> %d\n", i, term_info[i].rx_box, ret);

        ret = MboxRelease(term_info[i].rx_syscall_box);
        DP(DEBUG4,"Disk %d rx_syscall_box %d -> %d\n", i, term_info[i].rx_syscall_box, ret);

        ret = MboxRelease(term_info[i].tx_box);
        DP(DEBUG4,"Disk %d tx_box %d -> %d\n", i, term_info[i].tx_box, ret);

        ret = MboxRelease(term_info[i].tx_syscall_box);
        DP(DEBUG4,"Disk %d tx_syscall_box %d -> %d\n", i, term_info[i].tx_syscall_box, ret);

    }
}

/*!
    These are the values I want each process table entry to have at start up.

    I create a 0 slot private mailbox for each process table entry to
    be used for synchronization.
*/

void
initialize_process_entry(int index)
{
    process_table[index].pid = NOT_A_PID;

    int ret = MboxCreate(0, 0);
    if (ret < 0)
        KERNEL_ERROR("Creating private mailbox for table entry %d\n", index);

    process_table[index].box_ID = ret;
    process_table[index].expiry_time = -1;
    process_table[index].expiry_next = NULL;
    process_table[index].disk_next = NULL;

    process_table[index].disk_request.request_type = -42;
    process_table[index].disk_request.buffer = NULL;
    process_table[index].disk_request.track = -1;
    process_table[index].disk_request.first = -1;
    process_table[index].disk_request.sectors = -1;

    process_table[index].result = NULL;
}

/*!
    Set process table entries to a known state at startup.
*/

void
initialize_process_table(void)
{
    int i = 0;
    for ( ; i < MAXPROC; ++i)
        initialize_process_entry(i);
}

/*!
    Add handlers for new system calls to system call vector
*/

void
initialize_sysvec(void)
{
    /* other SYS_* are already set */
    sys_vec[SYS_SLEEP]      = sleep;
    sys_vec[SYS_DISKREAD]   = disk_read;
    sys_vec[SYS_DISKWRITE]  = disk_write;
    sys_vec[SYS_DISKSIZE]   = disk_size;
    sys_vec[SYS_TERMREAD]   = term_read;
    sys_vec[SYS_TERMWRITE]  = term_write;
}


void
initialize_clock_data(void)
{
    int ret;
    /* create 1 slot mailbox for clock driver process.  The absolute
     * time at which they should wake in microseconds is stored in
     * their process table entry (which can be found via clock_info's
     * list). If the desiring-to-sleep process calculates the time
     * before they do the send, then even if they block for awhile in
     * the send, they should be woken up appropriately (assuming sleep
     * time isn't absolutely tiny). */
    ret = MboxCreate(1, sizeof(int));
    if (ret < 0)
        KERNEL_ERROR("Creating mailbox for clock driver %d", ret);

    DP(DEBUG3, "Mutex for clock is %3d\n", ret);

    clock_info.mutex_ID = ret;
    clock_info.front = NULL;
    clock_info.back  = NULL;
}

/*!
    I have a mutex around the disk queue, and then a 0 slot box that
    the disk driver sleeps on when there are no requests queued.
*/

void
initialize_disk_data(void)
{
    int i, ret;
    for (i = 0; i < DISK_UNITS; ++i)
    {
        /* This one is what disk driver process will block on until
         * notified that there is a request pending */
        ret = MboxCreate(0, 0);
        if (ret < 0)
            KERNEL_ERROR("Creating mailbox for disk %d: %d", i, ret);
        disk_info[i].box_ID = ret;

        DP(DEBUG3,"Disk %d wake-up box ID %3d\n", i, ret);

        /* create mutexes to use as guards for request queues */
        ret = MboxCreate(1, sizeof(int));
        if (ret < 0)
            KERNEL_ERROR("Creating mutex for disk %d: %d", i, ret);
        disk_info[i].mutex_ID = ret;

        DP(DEBUG3,"Disk %d work queue mutex ID %3d\n", i, ret);

        disk_info[i].front = NULL;
        disk_info[i].back  = NULL;
    }
}

/*!
    4 boxes per terminal.

    1 box from Rx syscall process to Terminal receiver process.
    1 box from Terminal receiver process to Terminal interrupt process.
    1 box from Tx syscall process to Terminal transmitter process.
    1 box from Terminal transmitter process to Terminal interrupt process.

    Most are mutex (1 slot) semaphores, but due to the requirement to
    buffer received terminal data, the box from the Rx process to the
    Rx syscall is LINES_TO_BUFFER slots of MALINE size.
*/

void
initialize_term_data(void)
{
    int i, ret;

    /* create mailboxes for use by terminal device driver processes */
    for (i = 0; i < TERM_UNITS; ++i)
    {
        /* mailbox for int handler<->Rx processes */
        ret = MboxCreate(1, sizeof(char));
        if (ret < 0)
            KERNEL_ERROR("Creating Rx box for term %d: %d", i, ret);
        term_info[i].rx_box = ret;
        DP(DEBUG3, "Term %d rx_box is %d\n", i, ret);

        /* big mailboxes for Rx<->syscall processes */
        ret = MboxCreate(LINES_TO_BUFFER, MAXLINE);
        if (ret < 0)
            KERNEL_ERROR("Creating Rx box for term read syscall %d: %d", i,ret);
        term_info[i].rx_syscall_box = ret;
        DP(DEBUG3, "Term %d rx_syscall_box is %d\n", i, ret);

        /* mutex mailbox for Tx<->int handler processes */
        ret = MboxCreate(1, sizeof(int));
        if (ret < 0)
            KERNEL_ERROR("Creating Tx box for term %d: %d", i, ret);
        term_info[i].tx_box = ret;
        DP(DEBUG3, "Term %d tx_box is %d\n", i, ret);

        /* mailbox for Tx<->syscall processes */
        ret = MboxCreate(1, sizeof(data_line_t));
        if (ret < 0)
            KERNEL_ERROR("Creating Tx box for term %d: %d", i, ret);
        term_info[i].tx_syscall_box = ret;
        DP(DEBUG3, "Term %d tx_syscall_box is %d\n", i, ret);

        term_info[i].request.data       = 0;
        term_info[i].request.data_valid = 0;
        term_info[i].request.process    = NULL;
    }
}

