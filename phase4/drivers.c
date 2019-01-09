/*!
 *  Author: Robert Crocombe
 *  Class: CS452 Operating Systems Spring 2005
 *  Professor: Patrick Homer
 *
 *  The core driver processes routines.  A lot of the code is simply
 *  to ensure clean shutdown, plus the ever-abundant debugging
 *  printfs.
 *
 */

#include "drivers.h"
#include "utility.h"
#include "helper.h"
#include "types.h"

#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <usloss.h>

#include <stdlib.h>                 /* atoi */


/******************************************************************************/
/* Macros                                                                     */
/******************************************************************************/

/* Handling errors is tedious poo, and macros (and gotos!) help tame
 * it somewhat. */
#define DISK_ERR(ret,code,status,format, ...) if (ret != code) \
                                              { \
                                                status = -EDEVICE; \
                                                DP(DEBUG,format, ##__VA_ARGS__);\
                                                goto out; \
                                              }

#define TERM_ERR(ret,code,status,format, ...) if (ret != code) \
                                              { \
                                                status = -EDEVICE; \
                                                DP(DEBUG,format, ##__VA_ARGS__);\
                                                goto out; \
                                              }
/******************************************************************************/
/* Globals                                                                    */
/******************************************************************************/

extern proc_table_entry process_table[MAXPROC];

extern clock_info_t clock_info;
extern disk_info_t disk_info[DISK_UNITS];
extern term_info_t term_info[TERM_UNITS];

/******************************************************************************/
/* Prototypes for internal functions                                          */
/******************************************************************************/

static int handle_read_or_write( int unit,
                                 int disk_tracks,
                                 int *current_track,
                                 proc_table_entry *entry,
                                 disk_request_t *request);

static proc_table_entry * disk_scheduler(proc_table_entry *front,
                                         int unit,
                                         int disk_tracks,
                                         int current_track);

static int handle_rx_stuff(int rx_status, char data, int unit);
static int handle_tx_stuff(int tx_status, int unit);

static void check_for_expired(int now);

/******************************************************************************/
/* Kernel device drivers                                                      */
/******************************************************************************/

/*!
    Runs forever (until zapped), checking for sleeping processes and
    waking them up at the proper time.  Wakes up everybody when terminated.

    Figure out which clock from 'arg': default is 0
*/

int
clock_driver(char *arg)
{
    int unit = arg ? atoi(arg) : 0;
    int ret, status;
    proc_table_entry *p;

    DP(DEBUG4, "Clock is unit %d\n", unit);

    do
    {
        /* block on clock until next interrupt (5 ticks) */
        ret = waitdevice(CLOCK_DEV, unit, &status);
        if (ret == 0)
        {
            /* Success*/
            check_for_expired(sys_clock());
        } else
        {
            DP(DEBUG, "waitdevice failed on clock %d: %d\n", unit, ret);
            break;
        }
    } while (!is_zapped());

    DP(DEBUG3, "Purging clock list on terminate\n\n");

    /* wake up everybody */
    while (clock_info.front)
    {
        p = remove_from_expiry_list(clock_info.front);
        DP(DEBUG3,"Removing process %d from sleep list: sending to box %d\n",
                  p->pid, p->box_ID);
        ret = MboxSend(p->box_ID, 0, 0);
        DP(DEBUG3,"Result of send to pid %d box %d is %d\n",
                        p->pid, p->box_ID, ret);
    }

    DP(DEBUG, "Clock process terminating\n");

    return EOKAY;
}

/*!
    Handles read and write requests for disk 'unit'.  If a seek is
    required to handle the process, the new disk position is return
    via 'current_track'.

    'entry' points to the process that has made this request, and
    'request' is to the disk request structure within this entry.

    'disk_tracks' is the size of disk 'unit'.

    Possible return codes:

    -EDEVICE    if failure in device
    -EZAPPED    if zapped
    -EBADINPUT  if read/write takes us off end of disk
    EOKAY       if no problems

    EDEVICE indicates to the calling routine that it needs to
    retrieve the device's status register and return that as the
    return code for the syscall (from the spec).

    EBADINPUT is the return value required by the syscall for invalid
    inputs: given inputs that would result in accesses off the disk
    presumably fits the bill.
*/

int
handle_read_or_write
(
    int unit,
    int disk_tracks,
    int *current_track,
    proc_table_entry *entry,
    disk_request_t *request
)
{
    int i;
    int ending_sector,
        ret,
        status = -EZAPPED,
        track = request->track,
        sector_within_track = request->first;
    device_request disk_op;

    /* Decide upon legality of request */
    ending_sector = track == 0
                    ? request->first + request->sectors
                    : ((track - 1) * DISK_TRACK_SIZE) +
                      (request->first + request->sectors);

    if (ending_sector > (disk_tracks * DISK_TRACK_SIZE))
    {
        DP(DEBUG, "Request off end of disk: %d vs %d\n",
                  ending_sector, disk_tracks * DISK_TRACK_SIZE);
        status = -EBADINPUT;
        goto out;
    }

    /* one read per sector */
    for (i = 0; i < request->sectors; ++i)
    {
        /* decide whether must seek, and if so, do that */
        if (track != *current_track)
        {
            /* seek to proper track */
            disk_op.opr = DISK_SEEK;
            INT_TO_POINTER(disk_op.reg1, track);
            disk_op.reg2 = NULL;

            ret = device_output(DISK_DEV, unit, &disk_op);
            DISK_ERR(ret, DEV_OK, status,
                     "failed while seeking to %d on disk %d\n", track, unit);

            /* block until next disk interrupt (head is repositioned) */
            ret = waitdevice(DISK_DEV, unit, &status);
            HANDLE_ZAPPING(ret, status, EWAITDEVICEZAPPED);
            DISK_ERR(ret, EOKAY, status,
                     "waitdevice failed while seeking on disk %d\n",unit);

            /* Now at proper track.  Do read or write */
            *current_track = track;
        }

        /* send read/write request to disk */
        disk_op.opr  = request->request_type;
        INT_TO_POINTER(disk_op.reg1, sector_within_track);
        INT_TO_POINTER(disk_op.reg2, request->buffer + (i * DISK_SECTOR_SIZE));
        ret = device_output(DISK_DEV, unit, &disk_op);
        DISK_ERR(ret, DEV_OK, status,"Handling disk %d request: %d\n", unit, ret);

        /* block until next disk interrupt (request is serviced) */
        ret = waitdevice(DISK_DEV, unit, &status);
        HANDLE_ZAPPING(ret, status, EWAITDEVICEZAPPED);
        DISK_ERR(ret, EOKAY, status, "waitdevice failed on disk %d\n", unit);

        /* if reached end of track, move to 1st sector of next track
         * (requires a seek) */
        ++sector_within_track;
        if (sector_within_track == DISK_TRACK_SIZE)
        {
            ++track;
            sector_within_track = 0;
        }
    }   /* per sector read/write loop*/

    status = EOKAY;
out:
    DP(DEBUG4, "status == %d\n", status);
    return status;
}

/*!
    Circular scan: scan from inner edge of disk to outer edge, then
    start over at inner edge.

    Looks through queue of requests and takes the request closest to
    the current position in the direction of scan.  If there are no
    more requests in the direction of movement, start looking again
    from track 0 outward.
*/

proc_table_entry *
disk_scheduler
(
    proc_table_entry *front,
    int unit,
    int disk_tracks,
    int current_track
)
{
    int ret,
        mutex_ID = disk_info[unit].mutex_ID;
    proc_table_entry *selection = front;
    proc_table_entry *best = front;

    DP(DEBUG3, "disk %d current track is %d\n", unit, current_track);

    ret = get_mutex(mutex_ID);
    if (ret)
    {
        DP(DEBUG, "getting mutex %d for disk %d: %d\n", mutex_ID, unit, ret);
        goto out;
    }

    /* look for request closest to current position in the direction
     * of larger track numbers (toward outer edge of disk) */
    while (selection)
    {
        DP(DEBUG4, "Looking at request from %d\n", selection->pid);

        if (   (selection->disk_request.track >= current_track)
            && (selection->disk_request.track < best->disk_request.track))
        {
            /* toward outer edge of disk from current position, and
             * closer to where we are now (smallest seek) than current
             * best request (take oldest in case of ties) */
            best = selection;
        }
        selection = selection->disk_next;
    }

    /* No requests from here to outer edge of disk: find request
     * closest to inner edge */
    if (best->disk_request.track < current_track)
    {
        selection = front;
        best = front;
        while (selection)
        {
            DP(DEBUG4, "Inner out: looking at pid %d request\n",selection->pid);
            if (selection->disk_request.track < best->disk_request.track)
            {
                best = selection;
            }
            selection = selection->disk_next;
        }
    }

    ret = release_mutex(mutex_ID);
    if (ret)
        DP(DEBUG, "releasing mutex %d for disk %d: %d\n", mutex_ID, unit, ret);

out:
    DP(DEBUG3, "Decided to use request from pid %d to track %d\n",
               best->pid, best->disk_request.track);

    return best;
}

/*!
    Continuous checking for *one* disk, looking for input

    Figure out which disk from 'arg'

    Possible return codes:

    EOKAY       no errors
    -EZAPPED    zapped
    -EBADINPUT  request is off edge of disk
    -EDEVICE    device error
*/

int
disk_driver(char *arg)
{
    /* Apparently, magical forces have the disk seeking to a track
     * near/at the middle of the disk or some shit to start.  */
    int current_track = -42;

    /* size of disk*/
    int tracks;

    int unit = arg ? atoi(arg) : 0,
        ret,
        status = -EZAPPED;

    device_request disk_op = { .opr = DISK_TRACKS,
                               .reg1 = &tracks,
                               .reg2 = NULL };
    disk_info_t *disk = &disk_info[unit];
    proc_table_entry *entry, *p;
    disk_request_t *request;

    ret = device_output(DISK_DEV, unit, &disk_op);
    if (ret != DEV_READY)
        KERNEL_ERROR("Couldn't determine disk geometry for disk %d\n", unit);

    DP(DEBUG4, "Waiting to get disk geometry for disk %d\n", unit);

    /* block until next disk interrupt (request is serviced) */
    ret = waitdevice(DISK_DEV, unit, &status);
    HANDLE_ZAPPING(ret, status, EWAITDEVICEZAPPED);
    DISK_ERR(ret, EOKAY, status, "waitdevice failed on disk %d\n", unit);

    DP(DEBUG4,"Got disk %d size as %d tracks: %d\n", unit, tracks, ret);

    /* Now we know the disk's size in tracks: value's in 'tracks' */
    do
    {
        if (!disk->front)
        {
            DP(DEBUG, "Disk %d napping until there's a request\n", unit);
            /* when no requests, sleep. */
            ret = MboxReceive(disk->box_ID, 0, 0);
            HANDLE_ZAPPING(ret, status, EZAPPED);
            DISK_ERR(ret, EOKAY, status, "getting request for unit %d\n", unit);

            DP(DEBUG4, "Request received on disk %d\n", unit);

            /* Woken up, but no request was added.  Time to die?
             * (This happens at shutdown). */
            if (!disk->front)
            {
                DP(DEBUG, "Empty queue on disk %d, but woken up!?\n", unit);
                break;
            }
        }

        DP(DEBUG4, "Disk %d deciding upon request\n", unit);
        entry = disk_scheduler(disk->front, unit, tracks, current_track);
        remove_from_disk_list(entry, unit);
        request = &entry->disk_request;

        DP(DEBUG4, "Disk %d servicing request on disk %d from pid %d\n",
                   unit, entry->pid);

        switch (request->request_type)
        {
        case DISK_TRACKS:
            request->result = tracks;
            break;

        case DISK_READ:     /* fall through */
        case DISK_WRITE:
            ret = handle_read_or_write(unit, tracks, &current_track, entry, request);
            HANDLE_ZAPPING(ret, status, EZAPPED);
            request->result = ret;
            DP(DEBUG4, "disk %d result of read or write is %d\n", unit, ret);
            break;

        default:
            DP(DEBUG, "Unknown disk %d request type %d",
                      unit, request->request_type);
            status = -EDEVICE;
            goto out;
        }

        DP(DEBUG4, "Request from pid %d on disk %d complete\n",
                   entry->pid, unit);

        /* request handled, wake up requester */
        ret = MboxSend(entry->box_ID, 0, 0);
        HANDLE_ZAPPING(ret, status, EZAPPED);
        if (ret != 0)
        {
            DISK_ERR(ret, EOKAY, status,
                     "waking up requester %d on disk %d: %d\n",
                     entry->pid, unit, ret);
        }

    } while (!is_zapped());

    status = EOKAY;
out:

    /* All this crap is clean up */
    if (entry)
    {
    DP(DEBUG4, "disk %d freeing requester %d box %d\n",
                    unit, entry->pid, entry->box_ID);

    ret = MboxCondSend(entry->box_ID, 0, 0);
    if (ret != EOKAY)
        DP(DEBUG4, "disk %d problem freeing requester %d box %d\n",
                    unit, entry->pid, entry->box_ID);
    }

    while (disk->front)
    {
        p = remove_from_disk_list(disk->front, unit);
        DP(DEBUG3,"Removing process %d from disk list: sending to box %d\n",
                  p->pid, p->box_ID);
        ret = MboxSend(p->box_ID, 0, 0);
        DP(DEBUG3,"Result of send to pid %d box %d is %d\n",
                        p->pid, p->box_ID, ret);
    }

    DP(DEBUG4, "disk %d releasing wake-up box %d\n", unit, disk->box_ID);
    ret = MboxRelease(disk->box_ID);
    if (ret != EOKAY)
        DP(DEBUG4, "disk %d problem freeing wake-up box %d: %d\n",
                    unit, disk->box_ID, ret);
    else
        DP(DEBUG4, "disk %d released %d okay\n", unit, disk->box_ID);

    DP(DEBUG4, "disk %d releasing list mutex %d\n", unit, disk->mutex_ID);
    ret = MboxRelease(disk->mutex_ID);
    if (ret != EOKAY)
        DP(DEBUG4, "disk %d problem freeing disk queue mutex %d: %d\n",
                    unit, disk->mutex_ID, ret);
    else
        DP(DEBUG4, "disk %d released %d okay\n", unit, disk->mutex_ID);

    DP(DEBUG, "Disk %d driver terminating\n", unit);
    return status;
}

/*!
    Block on mailbox shared with terminal_driver.  As chars come in,
    buffer them into a line and conditionally send that line to
    another mailbox (from which syscall can retrieve data).

    The mailbox to which this sends data will support holding 10
    [LINES_TO_BUFFER] lines.  If a conditional send would block, then
    the mailbox is full, and we should do a read here before sending
    to discard the least recent (on front of mailbox queue) line of
    data.

    Possible return codes:

    EOKAY           no problems
    -EDEVICE   device problems
*/

int
terminal_receiver(char *arg)
{
    int ret,
        status      = EOKAY,
        position    = 0,
        unit        = atoi(arg);

    int to_syscall  = term_info[unit].rx_syscall_box,
        from_rx     = term_info[unit].rx_box;

    char data;
    char line[MAXLINE];
    char garbage[MAXLINE];

    DP(DEBUG4, "Term %d ('%s') Rx process: syscall box == %d, rx_box == %d\n",
               unit, arg, to_syscall, from_rx);

    do
    {
        /* new data from terminal device */
        ret = MboxReceive(from_rx , &data, sizeof(data));
        HANDLE_ZAPPING(ret, status, EZAPPED);
        TERM_ERR(ret, sizeof(data), status,
                 "term %d badness receiving from box %d: %d\n",
                 unit, from_rx, ret);

        DP(DEBUG4, "term %d receiver got '%c' for position %d\n",
                   unit, data, position);

        /* buffer up to a line of data */
        line[position] = data;
        ++position;

        if ((data == '\n') || (position == MAXLINE))
        {
            /* End of line.  Send data */
            DP(DEBUG4,"term %d end of line at position %d\n", unit, position);
            DP(DEBUG4, "term %d sending the following string: '", unit);
/*
            for (i = 0; i < position; ++i)
            {
                console("%c", line[i]);
            }
            console("'\n");
*/
            ret = MboxCondSend(to_syscall, line, position);
            HANDLE_ZAPPING(ret, status, EZAPPED);

            /* Nothing quit like being diligent about handling errors
             * in C, is there? Fucking whee. */
            switch (ret)
            {
            case -EWOULDBLOCK:

                /* buffer full.  Do read to erase data.  Then do send. */
                ret = MboxReceive(to_syscall, garbage, sizeof(garbage));
                HANDLE_ZAPPING(ret, status, EZAPPED);
                if ((ret < 0) || (ret > MAXLINE))
                {
                    DP(DEBUG, "term %d clearing old line of data: %d\n",
                               unit, ret);
                }

                ret = MboxSend(to_syscall, line, position);
                HANDLE_ZAPPING(ret, status, EZAPPED);
                TERM_ERR(ret, EOKAY, status,
                               "term %d sending buffered line: %d\n", unit,ret);
                break;

            default:
                TERM_ERR(ret, EOKAY, status,
                           "term %d sending buffered data: %d\n", unit, ret);
            }

            /* starting a new line */
            position = 0;
        }
        /* not end of line of data (anymore).  Keep going. */
    } while (!is_zapped());

    status = EOKAY;
out:

    /* cleaning up */
    DP(DEBUG4, "term %d rx freeing syscall box %d\n", unit, to_syscall);
    ret = MboxRelease(to_syscall);
    if (ret != EOKAY)
        DP(DEBUG4,"term %d: problem when releasing box %d: %d\n",
                  unit, to_syscall, ret);
    else
        DP(DEBUG4, "term %d rx freed box %d okay\n", unit, to_syscall);

    /* leave 'from_rx' to interrupt handler */

    DP(DEBUG, "term %d receiver terminating: %d\n", unit, status);
    return status;
}

/*!
    Takes buffered line from write syscall and sends to terminal
    device for transmission.

    Separate task from int handler code so that it can block where
    there're no requests pending (obviously interrupt handler can't do
    that, or it'd miss incoming characters).

    Possible return code:

    EOKAY
    -EZAPPED
    -EBADINPUT
*/

int
terminal_transmitter(char *arg)
{
    int i,
        ret,
        status,
        unit    = atoi(arg);

    int from_syscall = term_info[unit].tx_syscall_box;
    int mutex_ID = term_info[unit].tx_box;

    data_line_t line;
    term_request_t *request= &(term_info[unit].request);

    DP(DEBUG4,"Term %d Tx process: from_sycall == %d mutex == %d\n",
              unit, from_syscall, mutex_ID);

    do
    {
        DP(DEBUG4, "Term %d napping on %d until receives request\n",
                    unit, from_syscall);

        /* wake up once received request */
        ret = MboxReceive(from_syscall, &line, sizeof(data_line_t));
        HANDLE_ZAPPING(ret, status, EZAPPED);
        TERM_ERR(ret, sizeof(data_line_t), status,
                "Getting request from tx syscall on term %d using box %d: %d\n",
                unit, from_syscall, ret);

        DP(DEBUG4, "Term %d has received a request to xmit %d bytes\n",
                   unit, line.count);

        /* Enable Tx interrupt upon starting Tx job*/
        set_term_interrupts(unit, TX_ON);

        for (i = 0; i < line.count; ++i)
        {
            DP(DEBUG4, "Term %d: sending byte '%c' %d of %d\n",
                       unit, line.buffer[i], i, line.count);

            /* put data where int handler code can see to transmit it */
            ret = get_mutex(mutex_ID);
            TERM_ERR(ret, EOKAY, status,
                     "term %d getting Tx<->int mutex ID %d: %d",
                     unit, mutex_ID,ret);

            request->data_valid = 1;                    /* indicate freshness */
            process_table[CURRENT].pid = getpid();      /* oh yeah... */
            request->process = &process_table[CURRENT]; /* who to wake up */
            ret = release_mutex(mutex_ID);
            TERM_ERR(ret, EOKAY, status,
                    "term %d releasing Tx<->int mutex ID %d: %d",
                    unit, mutex_ID,ret);


            #define PREP_FOR_TX(s,d) ((int)((s & 0xFF) | ((0xFF & d) << 8)))

            ret = device_output(TERM_DEV,
                                unit,
                                (void *)(PREP_FOR_TX(0x07, line.buffer[i])));
            TERM_ERR(ret, DEV_OK, status,
                     "term %d: ERROR w/ transmit of '%c'\n",
                     unit, line.buffer[i]);

            /* wait for transmission to complete for this characer */
            ret = MboxReceive(process_table[CURRENT].box_ID, 0, 0);
            HANDLE_ZAPPING(ret, status, EZAPPED);

            DP(DEBUG4, "term %d tx of character '%c' acked by int handler\n",
                        unit, line.buffer[i]);
        }

        /* Disable Tx interrupt when doing naught */
        set_term_interrupts(unit, TX_OFF);

        DP(DEBUG4, "Term %d write complete: waking %d via box %d\n",
                   unit, line.process->pid, line.process->box_ID);

        line.process->result = i;
        /* wake up sender once transmission complete */
        ret = MboxSend(line.process->box_ID, 0, 0);
        HANDLE_ZAPPING(ret, status, EZAPPED);

    } while (!is_zapped());

    status = EOKAY;
out:

    /* purely clean up */

    /* Wake person whose request we were servicing when we got zapped */
    if ((status == -EZAPPED) && line.process)
    {
        DP(DEBUG4,"term %d attempting to free requester %d with box %d\n",
                      unit, line.process->pid, line.process->box_ID);
        ret = MboxCondSend(line.process->box_ID, 0, 0);
        if (ret != EOKAY)
            DP(DEBUG4,"term %d error freeing up requester %d with box %d: %d\n",
                      unit, line.process->pid, line.process->box_ID, ret);
        else
            DP(DEBUG4,"term %d freed requester %d with box %d... OK\n",
                      unit, line.process->pid, line.process->box_ID);
    }

    DP(DEBUG4,"term %d attempting to free syscall box %d\n", unit, from_syscall);
    ret = MboxRelease(from_syscall);
    if (ret != EOKAY)
        DP(DEBUG4, "term %d problem releasing syscall box %d: %d\n",
                   unit, from_syscall, ret);
    else
        DP(DEBUG4,"term %d freed syscall box %d... OK\n", unit, from_syscall);

    /* leave mutex_ID to interrupt handler */

    DP(DEBUG, "Term %d transmitter terminating: %d\n", unit, status);
    return status;
}

/*!
    Called by the terminal interrupt handler routine for terminal
    'unit'.  Looks at different Rx status codes from terminal status
    register, and then directs traffic based on rsult.  DEV_BUSY is
    the interesting one.  In that case, we have received a character,
    and should send it to the terminal receiver process.

    Possible return codes:

    EOKAY
    -EZAPPED
*/

int
handle_rx_stuff(int rx_status, char data, int unit)
{
    int ret,
        status,
        rx_box = term_info[unit].rx_box;        /* to Rx process */

    switch (rx_status)
    {
        case DEV_READY:
            /* No data waiting. Presumably interrupted for xmit */
            break;

        case DEV_ERROR:
            KERNEL_ERROR("Rx error for terminal %d", unit);
            break;

        case DEV_BUSY:
            /* Valid Rx data: send to receiver process */
            DP(DEBUG4,"Incoming char '%c' on term %d: to box %d\n",
                      data, unit, rx_box);
            ret = MboxCondSend(rx_box, &data, sizeof(data));
            HANDLE_ZAPPING(ret, status, EZAPPED);
            switch (ret)
            {
                case EOKAY: /* char sent okay */ break;
                case -EWOULDBLOCK:
                    /* They've missed this one... */
                    DP(DEBUG, "box %d full sending '%c' to Rx process for unit\n",
                              rx_box, data, unit);
                break;
                default: DP(DEBUG, "box send for term %d on box %d: %d\n",
                            unit, rx_box, ret);
            }
            break;

        default: /* Blah, nothing defined for this */
            KERNEL_ERROR("Unknown Rx status %d for unit %d", rx_status, unit);
    }
    status = EOKAY;
out:
    return status;
}

/*!
    Analagous to handle_rx_stuff, for the transmitter side of the terminal.

    Possible return codes:

    EOKAY           -- transmission fine or nothing happening
    -EZAPPED        -- zapped
    -EDEVICE        -- device error
*/

int
handle_tx_stuff(int tx_status, int unit)
{
    int ret,
        status      = EOKAY,
        data_valid  = 0,
        tx_box = term_info[unit].tx_box;        /* from Tx process */

    unsigned char data;
    term_request_t *request = &(term_info[unit].request);
    proc_table_entry *process;


    /* Now handle any xmit stuff */
    switch (tx_status)
    {
        case DEV_READY: /* Can send data if there's any to send */

            /* Get data if any.  Indicate that data has been retrieved */
            ret = get_mutex(tx_box);
            TERM_ERR(ret, EOKAY, status,
                       "Getting tx_box w/ ID %d: %d\n", tx_box, ret);

            data = request->data;
            data_valid = request->data_valid;
            process = request->process;
            request->data_valid = 0;
            ret = release_mutex(tx_box);
            TERM_ERR(ret, EOKAY, status,
                     "Releasing tx_box w/ ID %d: %d\n", tx_box, ret);

            if (data_valid)
            {
                    /* Tell transmitter that this byte has gone out.
                     * I guess that's not strictly true, because it
                     *  hasn't been acknowledged by interrupt, but I
                     *  think it's okay anyway. Otherwise there'd be
                     *  weird situations with 1st and/or last bytes. */
                    ret = MboxSend(process->box_ID, 0, 0);
                    HANDLE_ZAPPING(ret, status, EZAPPED);

                    DP(DEBUG, "term %d acking tx to box %d for pid %d\n",
                              unit, process->box_ID, process->pid);
            }
            /* else nothing waiting to be transmitted */
            break;
        break;

        case DEV_ERROR:
            KERNEL_ERROR("Tx error for terminal %d", unit);
            break;

        case DEV_BUSY: /* Terminal busy.  Cannot xmit character right now */
            break;

        default:
            KERNEL_ERROR("Unknown Tx status %d for unit %d", tx_status, unit);
    }

out:
    return status;
}

/*!
    Continuous checking for *one* terminal.  Upon any terminal
    interrupt, call the two subsidiary routines to decode the status
    info to see what/how to handle any requests or data.

    Figure out which term from 'arg'
*/

int
terminal_driver(char *arg)
{
    int unit = arg ? atoi(arg) : 0;
    int ret, rx_status, tx_status, status;
    char data;

    do
    {
        /* block until next terminal interrupt */
        ret = waitdevice(TERM_DEV, unit, &status);

        if (ret == 0)
        {
            /* good status info */
            data = TERM_STAT_CHAR(status);
            rx_status = TERM_STAT_RECV(status);
            tx_status = TERM_STAT_XMIT(status);

            ret = handle_rx_stuff(rx_status, data, unit);
            HANDLE_ZAPPING(ret, status, EZAPPED);

            ret = handle_tx_stuff(tx_status, unit);
            HANDLE_ZAPPING(ret, status, EZAPPED);
            TERM_ERR(ret, EOKAY, status, "device error in tx for term %d\n",
                     unit);
        } else
        {
            DP(DEBUG, "waitdevice failed on terminal %d\n", unit);
            break;
        }
    } while (!is_zapped());

    status = EOKAY;

out:

    /* purely clean up */
    DP(DEBUG4, "term %d releasing box to Tx task %d: %d\n",
                unit, term_info[unit].tx_box, ret);
    ret = MboxRelease(term_info[unit].tx_box);
    if (ret != EOKAY)
        DP(DEBUG4, "term %d problem releasing box to Tx task %d: %d\n",
                    unit, term_info[unit].tx_box, ret);
    else
        DP(DEBUG4, "term %d freed box to Tx task %d... OK\n",
                    unit, term_info[unit].tx_box);

    DP(DEBUG4, "term %d releasing box to Rx task %d: %d\n",
                unit, term_info[unit].rx_box, ret);
    ret = MboxRelease(term_info[unit].rx_box);
    if (ret != EOKAY)
        DP(DEBUG4, "term %d problem releasing box to Rx task %d: %d\n",
                    unit, term_info[unit].rx_box, ret);
    else
        DP(DEBUG4, "term %d freed box to Rx task %d... OK\n",
                    unit, term_info[unit].rx_box);

    DP(DEBUG, "Term %d int handler terminating\n", unit);
    return status;
}

/******************************************************************************/
/* Internal kernel helper routines.                                           */
/******************************************************************************/

/*!
    Will modify list if a process is to be woken, but in all cases we
    are traversing it, and if someone else is modifying it, bad things
    will happen.  Therefore must always take clock's queue mutex.
*/

void
check_for_expired(int now)
{
    int ret;
    proc_table_entry *p = clock_info.front;

    ret = get_mutex(clock_info.mutex_ID);
    if (ret)
    {
        DP(DEBUG, "getting clock mutex with ID %d: %d", clock_info.mutex_ID, ret);
        goto out;
    }

    while (p)
    {
        /* check if desired to sleep, and if so, if it's past wake up time */
        if (now > p->expiry_time)
        {
            remove_from_expiry_list(process_table + GET_SLOT(p->pid));

            /* Blocked process is [most likely] waiting on receive, so
             * we should not block There is a small chance, however,
             * so use conditional send (we'll pick it up next time: the
             * the badness should have cleared up by then) */
            DP(DEBUG4, "Waking up %d\n", p->box_ID);
            ret = MboxCondSend(p->box_ID, 0, 0);
            if ((ret != 0) && (ret != -EWOULDBLOCK))
                DP(DEBUG, "%d: problem waking up pid %d to be woken at %d "
                          "(diff %d) using box %d: %d\n",
                          now, p->pid, p->expiry_time,
                          now - p->expiry_time, p->box_ID, ret);
            /* reset expiry time */
            p->expiry_time = 0;
        }
        p = p->expiry_next;
    }
    ret = release_mutex(clock_info.mutex_ID);
    if (ret)
        DP(DEBUG, "releasing clock mutex with ID %d: %d", clock_info.mutex_ID, ret);

out:
    ;
}

