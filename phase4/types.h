#ifndef TYPES_H
#define TYPES_H

/*!
    Author: Robert Crocombe
    Class: CS452 Operating Systems Spring 2005
    Professor: Patrick Homer

    These are the types needed for this phase.  One is for the
    ubiquitous proces table, the others are largely used to convey
    requests between syscalls and driver processes, and there are per
    device type data structures: one each for the clock, disk, and
    terminal devices.
*/


/******************************************************************************/
/* Types                                                                      */
/******************************************************************************/

typedef struct _disk_request_struct
{
    /* I'll put results where the request type info was.  Don't need
     * it after results have been found, and I feel like saving 4
     * bytes per process table entry for no particular reason. */
    union
    {
        int request_type; /* DISK_READ | DISK_WRITE | DISK_SEEK | DISK_TRACKS */
        int result;
    };

    void *buffer;     /* data can go here */

    int track, first, sectors;
} disk_request_t;


typedef struct _proc_struct
{
    int pid;

    /* ID of a mailbox to be used for private communication */
    int box_ID;

    /* Time (in us) at which to wake up a sleeping process (else -1) */
    int expiry_time;

    struct _proc_struct *expiry_next;
    struct _proc_struct *disk_next;

    /* craptastical way of doing this */
    disk_request_t disk_request;
    int result;
} proc_table_entry;

/*!
    Everything needed to sleep safely.
*/

typedef struct _clock_info_struct
{
    int pid;                    /* who wants to nap */
    int mutex_ID;               /* data access atomicity */
    proc_table_entry *front;    /* sleepytime list front */
    proc_table_entry *back;     /* list rear */
} clock_info_t;

/*!
    Everything needed to access disk safely.
*/

typedef struct _disk_info_struct
{
    int box_ID,                 /* for waking up */
        mutex_ID;               /* for disk queue access atomicity */

    proc_table_entry *front, *back;
} disk_info_t;

/*!

*/

typedef struct _data_line_struct
{
    int count;                  /* how much to read or write*/
    unsigned char *buffer;      /* where it comes from/goes to*/
    proc_table_entry *process;  /* who made the request */
} data_line_t;

/*!
    How a character is organized to be transmitted
*/

typedef struct _term_request_struct
{
    int data_valid;             /* how to avoid stale data */
    proc_table_entry *process;  /* who is doing the transmitting */
} term_request_t;

/*!
    As single struct instead of array of smaller structs so that pids
    are in a single array so can use contains() routine.
*/
typedef struct _terminal_info_struct
{
    int rx_box,         /* int handler <-> Rx process */
        rx_syscall_box, /* Rx process <-> term read syscall: buffers data */
        tx_box,         /* int handler <-> Tx process */
        tx_syscall_box; /* single slot box of data_line_t for syscall <-> tx */

    term_request_t request; /* Holds data before Tx */
} term_info_t;

#endif  /* TYPES_H*/

