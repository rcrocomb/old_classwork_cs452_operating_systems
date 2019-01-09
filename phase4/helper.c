/*!
 *     Author: Robert Crocombe
 *     Class: CS452 Operating Systems Spring 2005
 *     Professor: Patrick Homer
 */

#include "helper.h"
#include "utility.h"
#include "libuser.h"
#include <phase2.h>         /* Mbox routines */

extern proc_table_entry process_table[];
extern clock_info_t clock_info;
extern disk_info_t disk_info[];

/******************************************************************************/
/* Clock-related routines                                                     */
/******************************************************************************/

/*!
    This handles its own locking.

    The reason the mutex stuff isn't in its own routine is because the
    macros record the line number of the file where they trigger, and
    that tells me exactly 0 if it happens within a locking subroutine:
    I want to know from which routine the locking was done.
*/

void
add_to_expiry_list(proc_table_entry *entry)
{
    int ret;
    if (!entry)
        KERNEL_ERROR("NULL kid");

    DP(DEBUG3, "%d Adding pid %d to expiry list with time %d\n",
               sys_clock(), entry->pid, entry->expiry_time);

    ret = get_mutex(clock_info.mutex_ID);
    if (ret)
        KERNEL_ERROR("getting clock mutex %d: %d",
                     clock_info.mutex_ID, ret);

    if (!clock_info.front) /* 1st to be enqueued */
    {
        clock_info.front = entry;
        clock_info.back = entry;
        entry->expiry_next = NULL;
    } else                  /* Add to end of queue */
    {
        clock_info.back->expiry_next = entry;
        clock_info.back = entry;
        entry->expiry_next = NULL;
    }

    ret = release_mutex(clock_info.mutex_ID);
    if (ret)
        KERNEL_ERROR("releasing clock mutex %d: %d",
                     clock_info.mutex_ID, ret);
}

/*!
    This *DOES* *NOT* handle its own locking, because it is called
    from within a routine that already needs the lock (whether this is
    ultimately called or not).  Can't double lock mutex, so...
*/

proc_table_entry *
remove_from_expiry_list(proc_table_entry *entry)
{
    proc_table_entry *p = NULL;
    proc_table_entry *previous = NULL;

    if (!entry)
        KERNEL_ERROR("entry  pointer is NULL\n");

    DP(DEBUG3, "%d Removing pid %d from expiry list with time %d\n",
               sys_clock(), entry->pid, entry->expiry_time);

    /* Find proc and element previous to it in queue */
    if (clock_info.front)
    {
        previous = p = clock_info.front;
        while (p && (p != entry))
        {
            previous = p;
            p = p->expiry_next;
        }
    }

    /* Remove element from list */
    if (p)
    {
        break_expiry_list(p, previous);
    } else
        KERNEL_ERROR("Couldn't find %d in expiry list\n", entry->pid);

    return p;
}

/*!
    Re-links the expiry list around 'p', and breaks all links from 'p'
    into the list.  'previous' is the pointer to the element in the
    list previous to 'p', else 'p'.
*/

void
break_expiry_list
(
    proc_table_entry *p,
    proc_table_entry *previous
)
{

    /* Found node and previous node: rejoin list around 'p' */
    if (p == previous)
    {
        DP(DEBUG5,"first\n");
        /* First in queue */
        clock_info.front = clock_info.front->expiry_next;
        /* Only in queue? */
        if (clock_info.back == p)
            clock_info.back = NULL;
    } else
    {
        DP(DEBUG5,"not first\n");
        /* Not first (and therefore not only) */
        previous->expiry_next = p->expiry_next;
        /* Last? */
        if (p == clock_info.back)
            clock_info.back = previous;
    }
}

/*!
    The disk has a queue-ish type list: pointers to front and back
    (where entries are added, so oldest requests are closer to the
    front.  But removal can be from everywhere, subject to the whims
    of the disk scheduling algorithm.
*/

void
add_to_disk_list(proc_table_entry *entry, int unit)
{
    disk_info_t *disk = &disk_info[unit];
    int ret,
        mutex_ID = disk_info[unit].mutex_ID;

    if (!entry)
        KERNEL_ERROR("NULL kid");

    DP(DEBUG3, "Adding pid %d to queue for disk %d\n", entry->pid, unit);

    ret = get_mutex(mutex_ID);
    if (ret)
        KERNEL_ERROR("getting disk %d mutex %d: %d", unit, mutex_ID, ret);
    if (!disk->front) /* 1st to be enqueued */
    {
        disk->front = entry;
        disk->back  = entry;
        entry->disk_next = NULL;
    } else                  /* Add to end of queue */
    {
        disk->back->disk_next = entry;
        disk->back = entry;
        entry->disk_next = NULL;
    }
    ret = release_mutex(mutex_ID);
    if (ret)
        KERNEL_ERROR("releasing disk %d mutex %d: %d", unit, mutex_ID, ret);
}

/*!
    Similar to the expiry list routine, removes a disk request from
    the disk list associated with disk 'unit'.
*/

proc_table_entry *
remove_from_disk_list(proc_table_entry *entry, int unit)
{
    proc_table_entry *p = NULL;
    proc_table_entry *previous = NULL;
    disk_info_t *disk = &disk_info[unit];
    int ret,
        mutex_ID = disk_info[unit].mutex_ID;

    if (!entry)
        KERNEL_ERROR("entry  pointer is NULL\n");

    DP(DEBUG3, "Removing pid %d from queue for disk %d\n", entry->pid, unit);

    ret = get_mutex(mutex_ID);
    if (ret)
        KERNEL_ERROR("getting disk %d mutex %d: %d", unit, mutex_ID, ret);


    /* Find proc and element previous to it in queue */
    if (disk->front)
    {
        previous = p = disk->front;
        while (p && (p != entry))
        {
            previous = p;
            p = p->disk_next;
        }
    }

    /* Remove element from list */
    if (p)
        break_disk_list(p, previous, disk);
    else
        KERNEL_ERROR("Couldn't find %d in disk list\n", entry->pid);

    ret = release_mutex(mutex_ID);
    if (ret)
        KERNEL_ERROR("releasing disk %d mutex %d: %d", unit, mutex_ID, ret);
    return p;
}

/*!
    Blah blah, actually relinks list around 'p', etc.
*/

void
break_disk_list
(
    proc_table_entry *p,
    proc_table_entry *previous,
    disk_info_t *disk
)
{

    /* Found node and previous node: rejoin list around 'p' */
    if (p == previous)
    {
        DP(DEBUG5,"first\n");
        /* First in queue */
        disk->front = disk->front->disk_next;
        /* Only in queue? */
        if (disk->back == p)
            disk->back = NULL;
    } else
    {
        DP(DEBUG5,"not first\n");
        /* Not first (and therefore not only) */
        previous->disk_next = p->disk_next;
        /* Last? */
        if (p == disk->back)
            disk->back = previous;
    }
}


/*!
    For single slot (mutex) semaphores.  Handily wraps up essential
    functionality and all that into a little routine.  It's not
    important what goes into this single slot, so I always make mine
    the size of an integer, and read and write garbage data to acquire
    the mutex.
*/

int
get_mutex(int mutex_ID)
{
    int garbage;
    DP(DEBUG5, "Acquiring mutex %d\n", mutex_ID);
    int ret = MboxSend(mutex_ID, &garbage, sizeof(garbage));
    return ret == sizeof(garbage) ? 0 : ret;
}

/*!
    Likewise, but in the opposite direction.
*/

int
release_mutex(int mutex_ID)
{
    int garbage;
    DP(DEBUG5, "Releasing mutex %d\n", mutex_ID);
    int ret = MboxReceive(mutex_ID, &garbage, sizeof(garbage));
    return ret == sizeof(garbage) ? 0 : ret;
}

