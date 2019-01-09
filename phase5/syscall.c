/*!
 *  Author: Robert Crocombe
 *  Class: CS452 Operating Systems Spring 2005
 *  Professor: Patrick Homer
 *
 *  User-facing part of the kernel: handles user<->kernel interface
 *  for the syscalls VmInit and VmCleanup.
 */

#include "syscall.h"
#include "utility.h"            /* KERNEL_MODE_CHECK, KERNEL_ERROR, lots... */
#include "types.h"              /* physical_page_t, PTE_t */
#include "vm_core.h"            /* initialize_page_list, release_paging... */
#include "pager.h"

#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>             /* int_vec */
#include <phase5.h>             /* MMU_* */
#include <provided_prototypes.h>/* terminate_real(), wait_real() */

#include <string.h>             /* memset */

/******************************************************************************/
/* Macros                                                                     */
/******************************************************************************/

#define PAGER_PRIO 2
#define PAGER_STACKSIZE (4 * USLOSS_MIN_STACK)

/******************************************************************************/
/* Global Variables                                                           */
/******************************************************************************/

extern void (*sys_vec[])(sysargs *args);
extern void (*int_vec[])(int dev, int unit);

extern swap_disk_t *swap_disk;
extern proc_table_t process_table[];
extern page_list_t *free_list;
extern byte *disk_buffer;
extern int fault_handler_queue;
extern int pager_daemon_pids[];
extern unsigned char *base_address;
extern int MMU_mutex;

/******************************************************************************/
/* Macros                                                                     */
/******************************************************************************/

/*
    Checks that are standard to each syscall function.  Note that
    check 4 only works because I call all my sysargs *s 'args'.  You
    could change this by supplying a 'c' parameter, I guess.

    I've added a test that Patrick seems to want (from page 6 of the
    hints and tips (phase 3 (2?))): that the various routines are
    being pointed to by the appropriate entry in the system call
    vector.  Seems a little odd to me, but WTF, yo.

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
/* Prototypes for internal functions                                          */
/******************************************************************************/

static void MMU_handler(int source, int offset);
static int get_from_disk(PTE_t *entry, void *destination);
static void *vm_init_real(int mappings, int virtual, int physical, int pagerd);
static int vm_cleanup_real(void);
static int daemon_cleanup(void);
static void *initialize_MMU(int mappings, int virtual_pages, int physical_pages);
static int fork_a_pager_daemon(int i);

/******************************************************************************/
/* Definitions                                                                */
/******************************************************************************/

/*!
    Start up virtual memory system.

    1st, get various necessary system parameters from the sysargs.

    2nd, check to see if they make any sense.  Only blatant errors
    will be caught.

    3rd, call the routine that does real work.

    4th, put back into sysargs return values as specified by instructions.
*/

void
vm_init(sysargs *args)
{
    int mappings_count;
    int virtual_pages;
    int physical_pages;
    int pager_daemons;
    void *address;

    STANDARD_CHECKS(SYS_VMINIT, vm_init);

    DP(DEBUG5, "started\n");

    mappings_count = INT_ME(args->arg1);
    virtual_pages = INT_ME(args->arg2);
    physical_pages = INT_ME(args->arg3);
    pager_daemons = INT_ME(args->arg4);

    /* assume failure */
    INT_TO_POINTER(args->arg4, -EINIT);

    if ((mappings_count < 0) || (mappings_count > MMU_NUM_TAG))
    {
        DP(DEBUG, "Illegal mappings count %d\n", mappings_count);
        goto out;
    }

    if (virtual_pages < 0)
    {
        DP(DEBUG, "Illegal virtual page count %d\n", virtual_pages);
        goto out;
    }

    if (physical_pages < 0)
    {
        DP(DEBUG, "Illegal physical page count %d\n", physical_pages);
        goto out;
    }

    /* XXX: for standard assignment.  physical == virtual */
    if (physical_pages != virtual_pages)
    {
        DP(DEBUG, "Not 1:1 relation twixt virtual && physical.  No cake!\n");
        goto out;
    }

    if ((pager_daemons < 0) || (pager_daemons > MAXPAGERS))
    {
        DP(DEBUG, "Illegal number of pager daemons %d\n", pager_daemons);
        goto out;
    }

    address = vm_init_real(mappings_count, virtual_pages, physical_pages, pager_daemons);
    if (address)
    {
        args->arg1 = address;
        INT_TO_POINTER(args->arg4, EOKAY);
    } else
        DP(DEBUG, "VM init failed\n");

out:
    ;
}

/*!
    Shut down the VM system.  Nothing to be done here except setting
    up sysargs.  Since there aren't any specifications for that, there
    aitn't much to do.

    I suppose that NULL sysargs would be fine here, but I still do a
    check.  Hrm.
*/

void
vm_cleanup(sysargs *args)
{
    STANDARD_CHECKS(SYS_VMCLEANUP, vm_cleanup);

    DP(DEBUG5, "clean up starting\n");
    vm_cleanup_real();
}

/*!
    Turns a virtual memory address into a virtual page address.
*/

int
offset_to_page(int offset)
{
    return offset / MMU_PageSize();
}

/******************************************************************************/
/* Definitions for internal functions                                         */
/******************************************************************************/

/*!
    The pid of the process that runs the MMU handler is the pid of the
    process that caused the fault, right?

    Gather information about fault, then direct to a pager daemon.
*/

void
MMU_handler(int source, int offset)
{
    int ret,
        virtual_page = offset_to_page(offset),
        physical_page,
        mapped_offset,
        protection;

    PTE_t *PTE_entry;
    MMU_fault_t fault;

    KERNEL_MODE_CHECK;

    if (source != MMU_INT)
        KERNEL_ERROR("running and interrupt not from MMU?");

    /* Enqueue fault: will not block */
    fault.pid = getpid();
    fault.cause = MMU_GetCause();
    fault.offset = offset;

    DP(DEBUG3, "fault pid %d offset %08x source %d.  Enqueueing\n",
               fault.pid, fault.offset, fault.cause);

    ret = MboxSend(fault_handler_queue, &fault, sizeof(fault));
    if (ret != 0)
    {
        DP(DEBUG,"error sending to fault queue: %d\n", ret);
        goto out;
    }

    /* Block until page entry is added: what is returned is *physical*
     * page ID */
    ret = MboxReceive(process_table[CURRENT].box_ID,
                      &physical_page,
                      sizeof(physical_page));

    if (ret != sizeof(physical_page))
    {
        DP(DEBUG, "error receiving on private box %d: %d\n",
                  process_table[CURRENT].box_ID, ret);
        goto out;
    }

    DP(DEBUG3, "Pager handler woke us: mapping is from vpage %d to ppage %d\n",
               virtual_page, physical_page);

    /* Okay, it's mapped.  What about the data in it?  If it's a new
     * page, then we need data set to 0.  If data is on disk, we need
     * to get that data read in.
     *
     * Works only because of 1:1
     */

    PTE_entry = &(process_table[CURRENT].table[virtual_page]);

    ret = MMU_GetMap(DEFAULT_TAG, virtual_page, &mapped_offset, &protection);
    if (ret != MMU_OK)
    {
        DP(DEBUG,"Couldn't get address of new physical page %d: '%s'\n",
                 physical_page, decode_MMU_warning(ret));
        goto out;
    }

    if (IS_PRESENT(PTE_entry))
    {
        DP(DEBUG3, "Setting physical page %d w/ offset %08x "
                    "(address %08x) to zeros\n",
                    physical_page, mapped_offset, ADDRESS(mapped_offset));

        /* The 'present' indicator here means this is a new memory
         * page (data isn't on disk and waiting to be retrieved), so we
         * should just zero the data values in the page. */
        memset(ADDRESS(mapped_offset), 0, MMU_PageSize());

        /* Gotta do some stat stuff here, right? This new page was
         * faulted in: either it's being written to or read from
         * (completely unitialized as far as they know!)
         *
         * I'm gonna call it a write, anyway...
         */
        MARK_WROTE(PTE_entry);
    } else
    {
        DP(DEBUG3, "Stuff for virtual page %d is swapped out.\n", virtual_page);

        /* Stuff isn't present in memory: it's on the disk.  Get it. */
        ret = get_from_disk(PTE_entry, ADDRESS(mapped_offset));
        if (ret != EOKAY)
            DP(DEBUG, "Error retrieving data from swap disk\n");

        DP(DEBUG3, "Got data for vpage %d into ppage %d from disk\n",
                   virtual_page, physical_page);
    }

    /* consider fault handled, dagnabit */
out:
    DP(DEBUG5, "Handler complete\n");
    ;
}

/*!
    Retrieve the info for the virtual page mapped by 'entry'.  'entry'
    contains info on where on the disk the data is stored.

    Return
        EOKAY on success
        -EDEVICE if the read from the disk fails
*/

int
get_from_disk(PTE_t *entry, void *destination)
{
    /* convert from page -> position info disk understands */
    int ret, track, sector, num_sectors;

    disk_page_to_geometry(entry->disk_location, &track, &sector, &num_sectors);

    DP(DEBUG3, "Reading to %08x from track %d sector %d for %d sectors\n",
                destination, track, sector, num_sectors);

    /* read data from the disk into the proper physical page */
    ret = disk_read_real(SWAP_DISK, track, sector, num_sectors, destination);
    if (ret != EOKAY)
    {
        DP(DEBUG, "Error retrieving data from disk.\n"
                  "Disk page == %d\n"
                  "Disk track == %d\n"
                  "Disk sector == %d\n"
                  "# of sectors to read == %d\n"
                  "Error code == %d\n",
                  entry->disk_location, track, sector,
                  num_sectors, ret);
    }

    INCREMENT_STAT(faults);
    INCREMENT_STAT(pageIns);
    MARK_READ(entry);
    MARK_PRESENT(entry);

    DP(DEBUG3, "Disk read complete for address %08x\n", destination);
    return ret == EOKAY ? EOKAY : -EDEVICE;
}

/*!
    Starting virtual memory subsystem consists of these steps:

    Setup MMU_INT handler queue -- done
    Setup physical pages list   -- done
    Setup disk buffer           -- done
    initialize VM statistics    -- done
    Fork pager daemons          -- done

    Returns:
        Address of VM region on success
        -EINIT on an error

*/

void *
vm_init_real(int mappings, int virtual_pages, int physical_pages, int pagerd)
{
    int ret,
        i = 0;
    void *address = NULL;

    vmStats.pages = virtual_pages;
    vmStats.frames = physical_pages;

    DP(DEBUG5, "Creating fault handler queue\n");

    /* As indicated, interrupts don't stack, so we need to create a
     * queue of pending interrupts.  No more than 1 interrupt per
     * proc, therefore MAXPROC entries suffices. */
    fault_handler_queue = MboxCreate(MAXPROC, sizeof(MMU_fault_t));
    if (fault_handler_queue < 0)
    {
        DP(DEBUG,"unable to allocate MMU fault queue: %d\n", fault_handler_queue);
        goto out;
    }

    DP(DEBUG5, "Creating physical page list for %d pages\n", physical_pages);

    /* sets up list of free pages showing all pages free initially:
     * list has built in mutual exclusion handling */
    ret = initialize_page_list(physical_pages);
    if (ret)
    {
        DP(DEBUG, "unable to create free page list: %d\n", ret);
        goto out;
    }

    DP(DEBUG5, "initializing MMU: allowed %d for %d to %d\n",
                mappings, virtual_pages, physical_pages);

    /* 'ret' will have start of VM area if this succeeds */
    address = initialize_MMU(mappings, virtual_pages, physical_pages);
    if (!address)
    {
        DP(DEBUG, "MMU failed to initialize\n");
        goto out;
    }

    DP(DEBUG5, "Initializing swap disk\n");

    /* retreive swap disk geometry and a few other useful things*/
    initialize_swap_disk();

    vmStats.blocks = swap_disk->disk_pages;
    vmStats.freeFrames = physical_pages;
    vmStats.freeBlocks = swap_disk->disk_pages;

    /* allocate the "outside VM area" disk buffer thingie: had to wait
     * until here to allocate because need valid page size (only after
     * initialize_MMU(). */
    disk_buffer = malloc(MMU_PageSize() * sizeof(byte));
    if (!disk_buffer)
    {
        DP(DEBUG,"failed to allocate disk buffer of %d bytes\n",
                 swap_disk->page_size);
        goto out;
    }

    /* initialize vm statistics struct -- not necessary since as a
     * static variable all fields are set to 0. */

    /* fork pager daemons */
    for ( ; i < pagerd; ++i)
    {
        DP(DEBUG3, "Forking %d of %d pager daemons\n", i, pagerd);
        pager_daemon_pids[i] = fork_a_pager_daemon(i);
    }

out:
    DP(DEBUG3,"VM initialized\n");
    return address;
}

int
fork_a_pager_daemon(int i)
{
    int pid;

    char pager_name[MAXNAME + 1];

    sprintf(pager_name, "%s%d", "pagerd", i);
    pid = fork1(pager_name, pager_daemon, NULL, PAGER_STACKSIZE, PAGER_PRIO);

    return pid;
}

/*!

    This should be done by each process, for itself, when they call
    p1_quit, which they will call as part of their termination.

        "Release all the virtual <-> physical memory mappings"

    Release the non-VM page/disk buffer thing
    Release the mailbox that has the queue of interrupt handler entries

    Release the free page list
    Release the mutex that guards the free page list
    Release the data structure that held these two elements

    Wait on pager daemons to quit.
*/

int
vm_cleanup_real(void)
{
    void *addy;
    int pages,
        ret,
        status = -ENOOP;

    /* Should do nothing if VmInit() has never done. */
    addy = MMU_Region(&pages);
    if (!addy)
        goto out;

    DP(DEBUG3, "Releasing process mailboxes\n");
        release_process_table_mailboxes();

    DP(DEBUG3, "Releasing disk buffer\n");
    SMART_FREE(disk_buffer);

    DP(DEBUG3, "Releasing paging disk allocation\n");
    release_paging_disk_allocations();

    DP(DEBUG3, "Releasing MMU fault queue mailbox\n");
    /* This should cause pager daemons to croak */
    MboxRelease(fault_handler_queue);

    DP(DEBUG3, "Waiting for pager daemons to quit\n");
    /* do waits for pager daemons */
    ret = daemon_cleanup();
    if (ret != MAXPAGERS)
        DP(DEBUG, "Wait only found %d of %d pagers\n", ret, MAXPAGERS);

    DP(DEBUG3, "Calling MMU_Done\n");
    /* Stop the MMU */
    ret = MMU_Done();
    if (ret != MMU_OK)
        DP(DEBUG, "error with MMU_Done: '%s'\n", decode_MMU_warning(ret));

    DP(DEBUG3, "Releasing free list mutex\n");
    MboxRelease(free_list->mutex_ID);
    SMART_FREE(free_list);

    DP(DEBUG3, "Releasing MMU mutex\n");
    MboxRelease(MMU_mutex);


out:
    return status;
}

/*!
    Wait for pager daemon processes to quit, or at least MAXPAGERS
    processes: hopefully it will be the pager daemons!
*/

int
daemon_cleanup(void)
{
    int i = 0,
        j,
        pagers_seen,
        status,
        pid;

    DP(DEBUG3, "Releasing paging daemons\n");

    /* Wait for MAXPAGERS tasks.  Hopefully they're all pager daemons,
     * but we'll quit regardless. */
    for ( ; i < MAXPAGERS; ++i)
    {
        pid = wait_real(&status);
        for (j = 0; j < MAXPAGERS; ++j)
        {
            if (pid == pager_daemon_pids[j])
            {
                ++pagers_seen;
                break;
            }
        }

        if (j == MAXPAGERS)
            DP(DEBUG, "wait process for non pager daemon process %d\n", pid);
        else
            DP(DEBUG3, "Pager daemon with pid %d has quit\n", pid);
    }
    DP(DEBUG3, "Done waiting for pager daemons  to terminate\n");

    return pagers_seen;
}

/*!
    Initialize MMU stuff.

    1st, set handler for MMU interrupts.

    After call to MMU_Init(), the current tag is set to 0.  This is
    what is wanted for non-extra credit case.

    Return
        VM address on success
        -EINIT on failure.
*/

void *
initialize_MMU(int mappings, int virtual_pages, int physical_pages)
{
    int status = -EINIT,
        ret,
        pages_count;
    void *address;



    /* Have we already initialized the MMU?  If so, then return
     * (VM Address) immediately */
    address = MMU_Region(&pages_count);
    if (address)
        return address;

    DP(DEBUG5, "Creating MMU mutex\n");
    /* get Mutex for MMU for stupid things that suck */
    MMU_mutex = MboxCreate(MUTEX_SLOTS, sizeof(int));
    if (MMU_mutex < 0)
        KERNEL_ERROR("Unable to create MMU mutex: %d\n", ret);

    DP(DEBUG5, "MMU mutex created: %d\n", MMU_mutex);
    DP(DEBUG5, "Setting MMU handler\n");

    /* Set MMU interrupt handler */
    int_vec[MMU_INT] = MMU_handler;

    DP(DEBUG5, "Calling MMU init\n");

    /* MMU has not been initialized (yet). */
    ret = MMU_Init(mappings, virtual_pages, physical_pages);
    if (ret != MMU_OK)
    {
        DP(DEBUG, "MMU error: %s\n", decode_MMU_warning(status));
        goto out;
    }

    ret = MMU_Unmap(0, 0);
    if (ret == MMU_ERR_NOMAP)
        DP(DEBUG, "Tag 0 virtual page 0 is NOT MAPPED\n");
    else
        KERNEL_ERROR("Why is page 0 at tag 0 already mapped?\n");

    DP(DEBUG5, "Getting VM start address\n");

    /* Okay, now it has been initialized.  Get address of start of VM area */
    address = MMU_Region(&pages_count);
    if (!address)
        DP(DEBUG, "MMU VM area failed to map?\n");

    base_address = (unsigned char *)address;

    DP(DEBUG3, "Base address is %08x\n", address);
out:
    return address;
}

