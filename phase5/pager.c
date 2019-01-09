/*!
    Author: Robert Crocombe
    Class: CS452 Operating Systems Spring 2005
    Professor: Patrick Homer

    The pager daemon plus one routine that turned out to be mostly
    unnecessary once I fixed a different part of the assignment.

    Pager daemon loops eternally and blocks on a queue of MMU
    interrupts.  These are handled as they come in by any of a
    multiple (up to MAXPAGERS) of these processes.
*/

#include <phase5.h>
#include <phase2.h>

#include "pager.h"
#include "types.h"
#include "utility.h"
#include "syscall.h"
#include "vm_core.h"

/******************************************************************************/
/* External global variables                                                  */
/******************************************************************************/

extern int fault_handler_queue;
extern proc_table_t process_table[];

/******************************************************************************/
/* Internal routine prototypes                                                */
/******************************************************************************/

static int check_mapping(MMU_fault_t *fault, PTE_t *mapping);

/******************************************************************************/
/* Definitions                                                                */
/******************************************************************************/

/*!
    Handles VM faults as they arrive in VM fault queue.  Loops
    infinitely until OS shuts down.

    returns
        EOKAY on successful shutdown
        -EMUCHBADNESS if there's any kind of failure.
*/

int
pager_daemon(char *arg)
{
    int status = -EMUCHBADNESS, ret;
    MMU_fault_t fault;
    PTE_t *table;
    PTE_t *mapping;

    KERNEL_MODE_CHECK;

    //DP(DEBUG3, "Pager daemon with pid %d started\n", getpid());
    DP(DEBUG3, "Pager daemon started\n");

    /* while OS is running */
    while (1)
    {
        DP(DEBUG4, "blocking on MMU handler request: box is %d\n",
                   fault_handler_queue);

        /* block on request from MMU handler: if box released: quit */
        ret = MboxReceive(fault_handler_queue , &fault, sizeof(fault));
        if (ret != sizeof(fault))
        {
            DP(DEBUG, "failure in pager fault queue receive: %d\n", ret);
            break;
        }

        if (fault.offset < 0)
        {
            DP(DEBUG, "Negative offset from pid %d type %d offset %d\n",
                      fault.pid, fault.cause, fault.offset);
            goto out;
        }

        /* At some point fault causes besides MMU_FAULT would
         * presumably be supported, and most of the gotos would go
         * away.   Not the best code for the time being :( */
        switch (fault.cause)
        {
        case MMU_PROT_NONE:
            DP(DEBUG, "Ouchies: fault is 'MMU_NONE' for pid %d offset %08x\n",
                      fault.pid, fault.offset);
            goto out;

        case MMU_FAULT:
            DP(DEBUG, "Fault for pid %d at %08x\n", fault.pid, fault.offset);
            break;

        case MMU_ACCESS:
            DP(DEBUG, "Access permissions for pid %d at offset %08x\n",
                      fault.pid, fault.offset);
            /* XXX */
            zap(fault.pid);
            goto out;
        default:
            DP(DEBUG, "Unkown MMU fault cause %d\n", fault.cause);
            goto out;
        }

        /* Get the PTE that is in charge of 'offset' */
        table = process_table[GET_SLOT(fault.pid)].table;
        mapping = &(table[offset_to_page(fault.offset)]);
/*
        if (check_mapping(&fault, mapping) != EOKAY)
            goto out;
*/

        /* Get physical page and add to this PTE entry.  Cannot add to MMU.
         *
         * The page is either (a) new (b) going to store stuff that's
         * currently swapped to disk.
         * */
        mapping->mapped_to = find_free_page();
        if (!mapping->mapped_to)
        {
            DP(DEBUG, "Couldn't get a free page\n");
            goto out;
        }

        /* setup reverse mapping (from physical page to PTE) */
        mapping->mapped_to->reverse = mapping;

        /* We cannot add to the MMU ourselves, because we aren't the
         * proper pid: a mapping would be for *our* virtual address,
         * not that of the pid that is making the request. So we can't
         * know if there is sufficient space available to make a map. */

        DP(DEBUG4, "Waking up process %d now that request is handled\n",
                   fault.pid);

        /* wake up process that generated fault.  Send them the
         * ID of the physical page that they are to use (it doesn't
         * really matter what we send, but that's a handy thing). */
        ret = MboxSend(process_table[GET_SLOT(fault.pid)].box_ID,
                       &(mapping->mapped_to->physical_page),
                       sizeof(mapping->mapped_to->physical_page));
        if (ret != 0)
        {
            DP(DEBUG, "MboxSend to pid %d box %d: %d\n",
                  fault.pid, process_table[GET_SLOT(fault.pid)].box_ID, ret);
            status = -EMUCHBADNESS;
            goto out;
        }
    }

    /* successful pager termination will be a 'break'.  Bad
     * termination will be a goto, so this assignment won't happen. */
    status = EOKAY;
out:
    //DP(DEBUG3, "Pager daemon with pid %d is quitting\n", getpid());
    DP(DEBUG3, "Pager daemon is quitting\n");
    return status;
}

/******************************************************************************/
/* Definitions for internal routines                                          */
/******************************************************************************/

/*!
    Test consistency of VM data structures

    returns:
        EOKAY if mapping stuff seems OK
        EMUCHBADNESS if mapping stuff has something awry
*/

int
check_mapping(MMU_fault_t *fault, PTE_t *mapping)
{
    int status = -EMUCHBADNESS;

    KERNEL_MODE_CHECK;

    if (!fault || !mapping)
        KERNEL_ERROR("Null pointer: fault %08x mapping %08x", fault, mapping);

    if (!mapping->mapped_to && !mapping->disk_location)         // 0 0
    {
        /* Mapping entry but no mapping data!  Much badness */
        DP(DEBUG, "for pid %d offset %08x: no physical address "
                  "or disk location.  Ouch\n", fault->pid, fault->offset);
        goto out;
    } else if (!mapping->mapped_to && mapping->disk_location)   // 0 1
    {
        /*  Good.  We have a disk location.  We need to get a
         *  physical frame now and then get the process to add
         *  it to MMU stuff and then get data into it. */

        /* i.e., keep going.  Only non-extra credit not an error case */
/*
        DP(DEBUG4, "For pid %d found disk location as %d\n",
                   fault->pid, mapping->disk_location);
*/

    } else if (mapping->mapped_to && !mapping->disk_location)   // 1 0
    {
        /* Ack.  This could happen afor MMU_ACCESS, but not
         * MMU_FAULT.  So it an extra-credit only thing? */
        DP(DEBUG, "pid %d offset %08x.  v <-> p mapping exists and "
                  "not swapped.  What's the problem?  "
                  "Fault cause is %d.  Terminating\n",
                  fault->pid, fault->offset, fault->cause);
        goto out;
    } else                                                      // 1 1
    {
        /* We have both physical memory and a disk location.
         * I think this can happen: we read a page from disk,
         * but make no changes to it.  It gets evicted.
         * Because there're no changes, if we still have the
         * location on disk, we can read it back from there
         * without having to write it out again first.
         *
         * But why the fault?  Must be to do with MMU_ACCESS.
         * I guess it's possible for a read-only page to get
         * swapped out.  We can read it back off disk, but we
         * can't make any changes to it.
         *
         * So I guess it's an error unless special credit.
         */
        DP(DEBUG, "pid %d offset %08x: has mapping and disk location. "
                  " But faulted w/ cause %d\n",
                  fault->pid, fault->offset, fault->cause);
        goto out;
    }


    status = EOKAY;
out:
    return status;
}

