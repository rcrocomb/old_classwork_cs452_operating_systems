/*!
 *  Author: Robert Crocombe
 *  Class: CS452 Operating Systems Spring 2005
 *  Professor: Patrick Homer
 *
 *  User-facing part of the kernel: handles user<->kernel interface
 *  for the syscalls Sleep, DiskRead, DiskWrite, DiskSize, TermRead,
 *  and TermWrite.
 */



#include "syscall.h"
#include "helper.h"
#include "utility.h"
#include "types.h"

#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <string.h>             /* memcpy */

/******************************************************************************/
/* Global Variables                                                           */
/******************************************************************************/

extern void (*sys_vec[])(sysargs *args);

extern proc_table_entry process_table[MAXPROC];
extern disk_info_t disk_info[];
extern term_info_t term_info[];

/******************************************************************************/
/* Macros                                                                     */
/******************************************************************************/

#define S_TO_US         ((int)1E6)

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
         matched to the value passed in via the sysarg struct member
'number'.

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

/******************************************************************************/
/* Prototypes                                                                 */
/******************************************************************************/

static void disk_stuff(sysargs *arg, int request_type);
static void term_stuff(sysargs *args, int type);

static int sleep_real(int seconds);

static int disk_stuff_real(int request_type,
                           int unit,
                           int track,
                           int first,
                           int sectors,
                           void *buffer);

static int disk_size_real(int unit);

static int term_read_real(int unit, int size, void *buffer);
static int term_write_real(int unit, int size, void *buffer);

/******************************************************************************/
/* Definitions                                                                */
/******************************************************************************/

/*!
    Checks arguments from Sleep() syscall.
*/

void
sleep(sysargs *args)
{
    int seconds, ret;

    STANDARD_CHECKS(SYS_SLEEP, sleep);

    /* set sysarg: assume failure by default */
    INT_TO_POINTER(args->arg4, -EBADINPUT);

    seconds = INT_ME(args->arg1);
    if (seconds < 0)
        DP(DEBUG,"Invalid seconds argument: %d\n", seconds);
    else
    {
        ret = sleep_real(seconds);
        if (ret)
            DP(DEBUG,"Non-zero return from sleep-real: %d\n", ret);
        ZERO_POINTER(args->arg4);
    }
}

/*!
    Really, I could have done this with a single routine and figured
    stuff out from syscall number, but I kinda like having the two
    routines, just in case.
*/

void
disk_read(sysargs *args)
{
    STANDARD_CHECKS(SYS_DISKREAD, disk_read);
    disk_stuff(args, DISK_READ);
}

/*!

*/

void
disk_write(sysargs *args)
{
    STANDARD_CHECKS(SYS_DISKWRITE, disk_write);
    disk_stuff(args, DISK_WRITE);
}

/*!
    Due to symmetry of read/write calls, a single routine can do the work.

    Checks as much of the validity of inputs as it can (doesn't know
    disk extents, so cannot see if writes will be off disk edge), and
    then calls the appropriate read/write routine (passed in as
    'func').  Checks return value to determine how to setup sysarg
    return values.

    unit == which disk to (write to, read from)
    track == starts writing/reading at this track
    first == the sector within track where the write/read begins
    sectors == # of sectors to write/read
    buffer == where data (goes to, comes from)
*/

void
disk_stuff(sysargs *args, int request_type)
{
    int sectors = INT_ME(args->arg2),
        track   = INT_ME(args->arg3),
        first   = INT_ME(args->arg4),
        unit    = INT_ME(args->arg5);
    void *buffer = args->arg1;
    int ret;

    /* set sysarg: assume failure by default */
     INT_TO_POINTER(args->arg4, -EBADINPUT);

    if ((unit < 0) || (unit >= DISK_UNITS))
    {
        DP(DEBUG,"Bad disk unit: %d\n", unit);
        goto out;
    }

    if (buffer == NULL)
    {
        DP(DEBUG, "NULL buffer pointer for disk %d request\n", unit);
        goto out;
    }

    if (sectors < 0)
    {
        DP(DEBUG, "Invalid number of sectors %d\n", sectors);
        goto out;
    }

    if (track < 0)
    {
        DP(DEBUG, "Invalid track %d\n", track);
        goto out;
    }

    if ((first < 0) || (first > DISK_TRACK_SIZE))
    {
        DP(DEBUG, "Invalid starting sector: %d\n", first);
        goto out;
    }

    ret = disk_stuff_real(request_type, unit, track, first, sectors, buffer);
    if (ret == EOKAY)       /* success */
    {
        INT_TO_POINTER(args->arg4, EOKAY);
    } else
    {
        if (ret == -EBADINPUT)
            INT_TO_POINTER(args->arg4, -EBADINPUT);

        /* at this point, who cares what this returns?  Not me. */
        (void) device_input(DISK_DEV, unit, &ret);
        DP(DEBUG, "Got disk status as requested for unit %d\n", unit);
    }

    /* 0 on success, or the status register elsewise */
    INT_TO_POINTER(args->arg1, ret);
out:
    DP(DEBUG4,"Returning arg1 as %08x arg4 as %d\n", args->arg1, args->arg4);
    ;
}

/*!
    Verifies what little info it can, then calls disk_size_real to put
    together actual disk request.
*/

void
disk_size(sysargs *args)
{
    int ret, unit;
    STANDARD_CHECKS(SYS_DISKSIZE, disk_size);

     unit = INT_ME(args->arg1);

    if ((unit < 0) || (unit >= DISK_UNITS))
    {
        DP(DEBUG, "Bad disk number %d\n", unit);
        INT_TO_POINTER(args->arg1, -EBADINPUT);
    } else
    {
        ret = disk_size_real(unit);
        if (ret >= 0)
        {
            INT_TO_POINTER(args->arg1, DISK_SECTOR_SIZE);
            INT_TO_POINTER(args->arg2, DISK_TRACK_SIZE);
            INT_TO_POINTER(args->arg3, process_table[CURRENT].disk_request.result);
            INT_TO_POINTER(args->arg4, EOKAY);
        }
    }
}

/*!

*/

void
term_read(sysargs *args)
{
    STANDARD_CHECKS(SYS_TERMREAD, term_read);
    term_stuff(args, SYS_TERMREAD);
}

/*!

*/

void
term_write(sysargs *args)
{
    STANDARD_CHECKS(SYS_TERMWRITE, term_write);
    term_stuff(args, SYS_TERMWRITE);
}

/*!
    Verifies that the arguments are correct, then calls
    term_stuff_real to do the heavy lifting.
*/

void
term_stuff(sysargs *args, int type)
{
    int ret, unit, count;
    void *buffer;

    INT_TO_POINTER(args->arg4, -EBADINPUT);

    buffer = args->arg1;
    count = INT_ME(args->arg2); /* size of read buffer or # of bytes to write */
    unit = INT_ME(args->arg3);

    if (buffer == NULL)
    {
        DP(DEBUG, "Null buffer pointer\n");
        goto out;
    }

    if (count < 0)
    {
        DP(DEBUG, "Bad buffer size of %d\n", count);
        goto out;
    }

    if ((unit < 0) || (unit >= TERM_UNITS))
    {
        DP(DEBUG, "Bad terminal unit of %d\n", unit);
        goto out;
    }

    if (type == SYS_TERMWRITE)
        ret = term_write_real(unit, count, buffer);
    else
        ret = term_read_real(unit, count, buffer);

    if (ret >= 0)  /* read okay: ret == # characters read/written */
    {
        INT_TO_POINTER(args->arg2, ret);
        INT_TO_POINTER(args->arg4, EOKAY);
    } else          /* problem */
        DP(DEBUG, "Failure on terminal %d: %d\n", unit, ret);

out:
    DP(DEBUG4,"term %d syscall terminating for %d\n", unit, getpid());
    ;
}

/******************************************************************************/
/* Functions that do real work                                                */
/******************************************************************************/

/*!
    There is an ugliness that could happen here.

    1) Added to expiry list with time very soon in future
    2) Pre-empted before receive
    3) Clock interrupt runs scan for expired tasks, and this task has expired.
    4) Tries to wake us, but we haven't received yet.  Since mailbox
       is a mutex, this would cause the alarm clock thingie to block, and
       no one would get woken up until this process ran.

    Therefore, I use a conditional send in the waker-upper.
    Eventually this task will resume, get to the receive, and be woken
    by the waker-upper on its pass through the list (it tries each
    pass until it succeeds).

    Possible return codes:

    EOKAY           everything was fine
    -EZAPPED        zapped while doing stuff
*/

int
sleep_real(int seconds)
{
    const int AS_USECONDS = seconds * S_TO_US;
    int status = -ENOTBLOODYLIKELY,
        ret;

    KERNEL_MODE_CHECK;

    /* absolute time in future (in usec) at which to wake */
    process_table[CURRENT].expiry_time = sys_clock() + AS_USECONDS;

    /* put expiry time onprocess_table[CURRENT] list (is in useconds) */
    process_table[CURRENT].pid = getpid();
    add_to_expiry_list(&process_table[CURRENT]);

    /* block on 0 slot mailbox.  Blocked process will be unblocked by
       send from device driver when time is up */
    ret = MboxReceive(process_table[CURRENT].box_ID, 0, 0);
    HANDLE_ZAPPING(ret, status, EZAPPED);

    status = EOKAY;
out:
    return status;
}

/*!
    Send a request to the disk driver synchronously, then block until
    request has been serviced.

    Possible return codes:

    EOKAY           everything completes fine
    -EBADINPUT      off end of disk with read or write
    -EZAPPED        zapped
    -EMUCHBADNESS   device problems
*/

int
disk_stuff_real
(
    int request_type,
    int unit,
    int track,
    int first,
    int sectors,
    void *buffer
)
{
    int ret,
        status = -ENOTBLOODYLIKELY;
    disk_request_t *request = &(process_table[CURRENT].disk_request);

    KERNEL_MODE_CHECK;

    request->request_type = request_type;
    request->buffer = buffer;
    request->track = track;
    request->first = first;
    request->sectors = sectors;

    /* enqueue request */
    process_table[CURRENT].pid = getpid();
    add_to_disk_list(&process_table[CURRENT], unit);

    /* Wake up disk. If disk is already awake (i.e., this send would
     * block), then it's okay. */
    DP(DEBUG4,"Wake disk %d on box %d\n", unit, disk_info[unit].box_ID);
    ret = MboxCondSend(disk_info[unit].box_ID, 0, 0);
    if (ret != -EWOULDBLOCK)
        HANDLE_ZAPPING(ret, status, EZAPPED);

    /* block until request completes */
    ret = MboxReceive(process_table[CURRENT].box_ID, 0, 0);
    HANDLE_ZAPPING(ret, status, EZAPPED);

    /* could be EOKAY, -EBADINPUT, or -EMUCHBADNESS */
    status = process_table[CURRENT].disk_request.result;
out:
    DP(DEBUG4,"returning result %d\n", status);
    return status;
}

/*!
    Retrieve number of tracks on disk 'unit'.

    -EBADINPUT
    #tracks on the disk
*/

int
disk_size_real(int unit)
{
    int ret, track_count = -EBADINPUT;

    KERNEL_MODE_CHECK;

    process_table[CURRENT].disk_request.request_type = DISK_TRACKS;
    process_table[CURRENT].pid = getpid();
    add_to_disk_list(&process_table[CURRENT], unit);

    /* block until request serviced  */
    ret = MboxReceive(process_table[CURRENT].box_ID, 0, 0);
    if (ret == EOKAY)
        track_count = process_table[CURRENT].disk_request.result;
    else if (ret == -EZAPPED)
        DP(DEBUG, "Zapped\n");
    else
        DP(DEBUG, "Error in box receive: %d\n", ret);

    return track_count;
}

/*!
    Possible return codes:

    EOKAY       if no data
    -EZAPPED    if zapped
    -EBADINPUT
    # of characters read, otherwise (cannot be 0).

    From test 01 or 02, I forget, it appears that 0 characters is not
    a possible value, because the test seemed to be expecting a
    string, but initially I was using CondReceive, and because the
    data hadn't arrived at the time of the call, was returning 0
    bytes.  In order for the test to make sense, therefore, I have
    changed the Receive to be unconditional.

    Get data if any is available: block if there is no data.

    By 'get data', I mean retrieve one line.  This is either MAXLINE
    characters if there was no newline, or less than that if a newline
    was received, or up to buffer_size if that is the smallest value:
    everything between the end of the last line and the newline is
    returned (including the newline), so you'll lose data if
    buffer_size < length of the line.
*/

int
term_read_real(int unit, int buffer_size, void *buffer)
{
    int ret, i, copy_size, status = -ENOTBLOODYLIKELY;
    char local_buffer[MAXLINE];

    DP(DEBUG4, "Got read request for term %d for <= %d bytes into %08x\n",
               unit, buffer_size, buffer);

    ret = MboxReceive(term_info[unit].rx_syscall_box, local_buffer, sizeof(local_buffer));
    HANDLE_ZAPPING(ret, status, EZAPPED);
    /* -EBADINPUT | #bytes retrieved */

    copy_size = buffer_size < ret ? buffer_size : ret;
    memcpy(buffer, local_buffer, copy_size);
    memset(local_buffer, 0, sizeof(local_buffer));

    DP(DEBUG4, "Read completed for %d (really %d) bytes on term %d\n'",
               copy_size, ret, unit);
/*
    for (i = 0; i < ret; ++i)
    {
        console("%c", ((char *)buffer)[i]);
    }
    console("'\n");
*/
    status = copy_size;
    if (status == -EBADINPUT)
        DP(DEBUG, "'bad input' on cond rx from box %d\n",
                  term_info[unit].rx_syscall_box);

out:
    DP(DEBUG4, "term %d read request completed: %d\n", unit, status);
    return status;
}

/*!
    Sets up a a terminal write request data strucure, then sends that
    structure to the Terminal transmitter task associated with
    terminal 'unit'.  Returns only once the request is complete.

    Possible return codes:

    EOKAY
    -EBADINPUT
    -EZAPPED
*/

int
term_write_real(int unit, int count, void *buffer)
{
    int ret,
        status = -ENOTBLOODYLIKELY;

    KERNEL_MODE_CHECK;

    DP(DEBUG4, "Get request for write to term %d of %d bytes from %08x\n",
                unit, count, buffer);

    /* submit terminal request */
    process_table[CURRENT].pid = getpid();
    data_line_t job = { .count = count,
                        .buffer = buffer,
                        .process = &process_table[CURRENT] };
    ret = MboxSend(term_info[unit].tx_syscall_box, &job, sizeof(job));
    HANDLE_ZAPPING(ret, status, EZAPPED);

    DP(DEBUG4, "Request sent to term %d: awaiting completion\n", unit);

    /* block until request complete */
    ret = MboxReceive(process_table[CURRENT].box_ID, 0, 0);
    HANDLE_ZAPPING(ret, status, EZAPPED);

    status = process_table[CURRENT].result;
out:
    DP(DEBUG4, "term %d write request completed: %d\n", unit, status);
    return status;
}

