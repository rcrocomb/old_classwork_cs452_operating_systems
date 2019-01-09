/*!
    Author: Robert Crocombe
    Class: CS452 Spring 2005
    Professor: Patrick Homer

    The guts of the VM code.  Initializes, looks at stuff and decides
    who should be swapped out and when, and suchlike.  See comments
    for individual routines.
*/

#include <phase2.h>                 /* Mbox* */
#include <phase5.h>                 /* MMU_* */
#include <provided_prototypes.h>    /* disk_*_real() */

#include "vm_core.h"
#include "types.h"
#include "utility.h"

#include <stdlib.h>
#include <string.h>                 /* memcpy() */

/******************************************************************************/
/* Globals                                                                    */
/******************************************************************************/

extern page_list_t *free_list;
extern swap_disk_t *swap_disk;
extern proc_table_t process_table[];
extern byte *disk_buffer;
extern unsigned char *base_address;
extern int MMU_mutex;

/******************************************************************************/
/* Internal Prototypes                                                        */
/******************************************************************************/

static int get_swap_write_location(void);
static void swap_out_new(physical_page_t *p);
static int free_a_page(void);
static void swap_and_free(physical_page_t *p);
static void write_out_dirty_page(physical_page_t *p);

static void initialize_process_table(void);
static void initialize_process_table_entry(int index);

/******************************************************************************/
/* Definitions                                                                */
/******************************************************************************/

/*!

    'physical_pages' -- total amount of physical memory available

    Okay, so we treat the flexible array of pages as both an array and
    as as singly-linked list.

    It's treated as an array when scanning for a page to free, i.e. by
    the clock algorith,

    Elements of the array are treated as a singly linked list of
    unused physical pages.  The head of the list is 'free_head' in the
    list struct, and then each physical_page_t has a next pointer.

    Returns:
        0 if okay
        -EMALLOC if memory allocation fails
        -EBADBOX if mutex allocation for free page list fails
*/

int
initialize_page_list(int physical_pages)
{
    int ret,
        i = 0,
        status = 0;

    KERNEL_MODE_CHECK;

    initialize_process_table();

    /* Create mutex for free page list access. */
    ret = MboxCreate(MUTEX_SLOTS, sizeof(int));
    if (ret < 0)
    {
        KERNEL_WARNING("failed to create mutex for free pages list: %d\n", ret);
        status = -EBADBOX;
        goto out;
    }

    free_list = malloc(sizeof(page_list_t)
                       + physical_pages * sizeof(physical_page_t));
    if (!free_list)
    {
        KERNEL_WARNING("Malloc for freelist of %d entries failed\n",
                       physical_pages);
        status = -EMALLOC;
        goto out;
    }

    free_list->mutex_ID = ret;
    free_list->page_count = physical_pages;
    free_list->clock_hand = 0;

    /* Initialize the data in the physical page data structures. */
    for ( ; i < physical_pages; ++i)
    {
        free_list->pages[i].physical_page = i;
        MARK_FREE(&(free_list->pages[i]));

        /* The array is now also a singly-linked list. */
        if ((i + 1) < physical_pages)
            free_list->pages[i].next = &(free_list->pages[i + 1]);
        else
            free_list->pages[i].next = NULL;

        free_list->pages[i].reverse = NULL;
    }

    /* pointer to 1st unused memory element */
    free_list->free_head = free_list->pages;

out:
    return status;
}

/*!
    Allocate space for swap disk usage recording structure, and
    retrieve info that'll be necessary for figuring out where swapped
    out data can/should go.
*/

void
initialize_swap_disk(void)
{
    int ret,
        i,
        page_size = MMU_PageSize(),
        sectors_per_page,
        sector_size,
        track_size,
        disk_bytes,
        disk_pages,
        tracks_count;

    /* get disk geometry */
    ret = disk_size_real(SWAP_DISK, &sector_size, &track_size, &tracks_count);
    if (ret != 0)
        KERNEL_ERROR("Retrieving swap disk parameters");

    DP(DEBUG5, "tracks %d sectors per track %d bytes per sector %d\n", tracks_count, track_size, sector_size);

    /* If a page < sector in size (unlikely), then we need to use at
     * least 1 sector. If a page is something like 1.5 sectors in
     * size, then the middle line will get us that extra sector. */
    sectors_per_page = page_size / sector_size; /* # sectors to hold a page */

    if (sectors_per_page == 0)
        sectors_per_page = 1;       /* must use at least 1 sector */
    else if ((page_size % sector_size) != 0)
        ++sectors_per_page;         /* fractional sectors: round up */

    disk_bytes = (sector_size * track_size * tracks_count);
    disk_pages = disk_bytes / page_size;

    DP(DEBUG5, "disk bytes == %d disk pages == %d sectors_per_page %d\n", disk_bytes, disk_pages, sectors_per_page);

    /* One integer for each page possible on the disk.  There
     * may be some empty disk bytes at the end if the disk size
     * modulo the page size is not 0. */
    swap_disk = malloc(sizeof(swap_disk_t) + (sizeof(int) * disk_pages));
    if (!swap_disk)
        KERNEL_ERROR("Failed to allocate memory for %d swap entries",
                     disk_pages);

    /* nothing is used yet */
    for (i = 0; i < disk_pages; ++i)
        swap_disk->usage[i] = NOT_A_PAGE;

    /* Now put all calculated junk in global data structure */

    swap_disk->page_size = page_size;
    swap_disk->disk_bytes = disk_bytes;
    swap_disk->disk_pages = disk_pages;
    swap_disk->sectors_per_page = sectors_per_page;
    swap_disk->sector_size = sector_size;
    swap_disk->track_size = track_size;
    swap_disk->tracks_count = tracks_count;
}

/*!
    Given the pid 'pid, creating a page table suitable for holding
    'table_size' entries.

    Set flags for entries to indicate a "fresh" state.
*/

void
initialize_page_table(int pid, int table_size)
{
    int i = 0;
    PTE_t *entry;
    PTE_t *table;

    if (process_table[GET_SLOT(pid)].table)
        KERNEL_ERROR("Existing table for process %d\n", pid);

    process_table[GET_SLOT(pid)].table = malloc(table_size);
    if (!process_table[GET_SLOT(pid)].table)
        KERNEL_ERROR("Mallocing page table for pid %d\n", pid);

    DP(DEBUG5, "for pid %d creating page table w/ %d entries\n",
               pid, table_size);

    table = process_table[GET_SLOT(pid)].table;
    for ( ; i < table_size; ++i)
    {
        entry = &table[i];

        entry->mapped_to = NULL;
        entry->disk_location = NOT_ON_DISK;
        CLEAR_ALL_FLAGS(entry);

        /* why not */
        MARK_READABLE(entry);
        MARK_WRITABLE(entry);
        /* not on disk as such */
        MARK_PRESENT(entry);
    }
}

/*!
    If returns NULL we're out of physical memory.  Otherwise returns
    pointer to the entry for a physical page.
*/

physical_page_t *
find_free_page(void)
{
    int garbage, ret;
    physical_page_t *new_page;

    KERNEL_MODE_CHECK;

    /* lock mutex so we can modify list */
    ret = MboxSend(free_list->mutex_ID, &garbage, sizeof(garbage));
    if (ret < 0)
    {
        DP(DEBUG, "Couldn't acquire free list mutex: %d\n", ret);
        goto out;
    }

    DP(DEBUG3, "looking for some free physical memory\n");

    /* head is NULL if we have no more free pages */
    if (!free_list->free_head)
    {
        DP(DEBUG, "No physical memory free\n");

        ret = free_a_page();
        if (ret != EOKAY)
        {
            DP(DEBUG,"Freeing up some memory failed\n");
            goto out;
        }
    }

#if 1
    /* count pages on list */
    int count = 0;
    new_page = free_list->free_head;
    while (new_page)
    {
        DP(DEBUG, "Page %d is in list at position %d\n", new_page->physical_page, count);
        ++count;
        new_page = new_page->next;
    }
    DP(DEBUG, "Found %d pages on free list\n", count);
#endif

    /* take 1st physical page entry off list.  We'll use that */
    new_page = free_list->free_head;
    free_list->free_head = free_list->free_head->next;
    new_page->next = NULL;

    MARK_NOT_FREE(new_page);
    DECREMENT_STAT(freeFrames);
out:

    /* unlock mutex */
    ret = MboxReceive(free_list->mutex_ID, &garbage, sizeof(garbage));
    if (ret != sizeof(garbage))
    {
        DP(DEBUG, "Failed to release free list mutex: %d\n", ret);
    }

    DP(DEBUG3, "Using free physical page %d: %d of %d left\n",
               new_page->physical_page, vmStats.freeFrames, vmStats.frames);
    return new_page;
}

/*!
    Mark the physical_page_t pointed to be 'to_free' as free, and
    insert it into the list of free pages.

    lock already acquired elsewhere.
*/

void
release_page(physical_page_t *to_free)
{
    int i = 0;

    /* let the guy who used to have this page mapped know that he
     * doesn't anymore.  He doesn't have it mapped since he's not the
     * running process. */
    to_free->reverse->mapped_to = NULL;

    /* Set other fields to sane values */
    to_free->reverse = NULL;
    MARK_FREE(to_free);
    INCREMENT_STAT(freeFrames);

    /* put this newly-freed frame back on the list of free frames */

    for ( ; i < free_list->page_count; ++i)
    {
        /* Here's a physical page that is in the list of free pages. */
        if (IS_FREE(&(free_list->pages[i])))
        {
            /* insert self into list (where exactly isn't known).
             * Do this by pointing this free element to to_free, and
             * to_free to what that free element was pointing to (so
             * now to_free is in list after this element that we've
             * found). Because insert is after, we can't screw up the
             * free_head pointer */
            to_free->next = free_list->pages[i].next;
            free_list->pages[i].next = to_free;

            break;
        }
    }

    /* This is the only free page (previously were none free) */
    if (free_list->free_head == NULL)
    {
        DP(DEBUG3, "page %d was the only free page: now at head of list\n",
                    to_free->physical_page);
        free_list->free_head = to_free;
        to_free->next = NULL;
    }

    DP(DEBUG3, "Freed frame %d: %d of %d now free\n",
                to_free->physical_page, vmStats.freeFrames, vmStats.frames);
}

/*!
    For use when shutting down.
*/

void
release_paging_disk_allocations(void)
{
    DP(DEBUG3, "Releasing paging disk allocations\n");
    SMART_FREE(swap_disk);
}


/*!

*/

void
mark_disk_page_clear(int page)
{
    if (page >= 0)
    {
        swap_disk->usage[page] = NOT_A_PAGE;
        INCREMENT_STAT(freeBlocks);
    }
    else
        DP(DEBUG, "Negative page argument %d\n", page);
}

/*!
    Given a disk page number, convert into track/sector/blah.
*/

void
disk_page_to_geometry(int disk_page, int *t, int *s, int *c)
{
    int track, first_sector, sector_count;

    int disk_sector = disk_page * swap_disk->sectors_per_page;

    /* disk pages can go over track boundaries.  If that's the case,
     * then track will truncate.  When we then recalculate the
     * starting sector, this truncation will make first_sector
     * okay.  E.g., with starting sector 12 and 10 sectors per track:
     *
     * 12 / 10 = track to use is 1.
     * 12 - (10 * 1) = first sector is 2.
     */
    track           = disk_sector / swap_disk->track_size;
    first_sector    = disk_sector - (track * swap_disk->track_size);
    sector_count    = swap_disk->sectors_per_page;

    *t = track;
    *s = first_sector;
    *c = sector_count;

    DP(DEBUG5, "Conversion from %d yields t %d s %d count %d\n",
               disk_page, *t, *s, *c);
}

/******************************************************************************/
/* Internal Definitions                                                       */
/******************************************************************************/

/*!
    This is fucking dumb.  I shouldn't have to manage the swap disk at
    this level.  I should simply say: write this much data from here
    to disk.  Tell me where you put it.  I *should* *not* have to tell
    the disk where on the disk to put it.

    This finds a free swap area and returns the track/sector/count for
    a write.

    Return
        >= 0: physical page at which to write data
        -ESWAPFULL if swap disk is full.
*/

int
get_swap_write_location(void)
{
    int status          = -ESWAPFULL,
        present_page    = 0;

    DP(DEBUG5, "Looking for a disk page to use as backing store\n");

    /* check through all page-sized area on the swap disk looking for
     * an unused spot.  Scans from lowest # to highest # */
    for ( ; present_page < swap_disk->disk_pages; ++present_page)
    {
        if (swap_disk->usage[present_page] == NOT_A_PAGE)
        {
            /* page isn't used */
            break;
        }
    }

    if (present_page == swap_disk->disk_pages)
    {
        DP(DEBUG, "You filled the swap disk.\n");
        goto out;
    }

    /* Mark this disk page as used. */
    ++(swap_disk->usage[present_page]);
    DECREMENT_STAT(freeBlocks);

    status = present_page;
out:
    DP(DEBUG3, "Using disk page %d for swap\n", present_page);
    return status;
}

/*
    Write data that has never been written to disk before to disk.

    To do this, we need access to data.  To get this, we have to map
    the page of the process' data into our address space.  This
    shouldn't be a problem: they don't have it mapped in MMU currently
    because they aren't running, so there'll be no conflict.

    Then we write stuff to disk.

    Then we unmap the page.
*/

void
swap_out_new(physical_page_t *p)
{
    int physical_page = 0;

    /* must check p first */
    if (!p || !p->reverse)
        KERNEL_ERROR("NULL pointer: page %08x reverse %08x\n",
                     p, (p ? (int)p->reverse: 0x1BADCAFE));

    /* have access to data: figure out where to write it */
    physical_page = get_swap_write_location();
    if (physical_page < EOKAY)
    {
        DP(DEBUG, "Failed to get spot to use as swap: %d\n", physical_page);
        goto out;
    }

    /* fill in page info, then call to disk writing routine */
    p->reverse->disk_location = physical_page;
    write_out_dirty_page(p);
    INCREMENT_STAT(new);
out:
    ;
}



/*!
    Clock algorithm for page selection

    I put the free_list->clock_hand in a local variable just so I can
    see what's going on (shorter line lengths and less pointer
    obfuscation)

    Returns
        EOKAY if memory freed up okay
        -EMUCHBADNESS elsewise
*/

int
free_a_page(void)
{
    int status = -EMUCHBADNESS,
        clock_hand = free_list->clock_hand;
    physical_page_t *p = NULL;

    KERNEL_MODE_CHECK;

    if (free_list->free_head)
    {
        DP(DEBUG, "Asked to free physical page, but there are free pages!\n");
        goto out;
    }

    DP(DEBUG3, "Out of physical memory: must free a page\n");

    /* the clock algorithm (hopefully) */
    p = &(free_list->pages[clock_hand]);
    while (IS_REFERENCED(p->reverse))
    {
        DP(DEBUG5, "Examined page %d.  Nah\n", p->physical_page);

        /* referenced recently.  Change to unreferenced, but don't use
         * this page yet.  This is the 'second chance' part of the algorithm. */
        MARK_UNREFERENCED(p->reverse);

        /* advance to the next page */
        clock_hand = (clock_hand + 1) % free_list->page_count;
        p = &(free_list->pages[clock_hand]);
    }

    DP(DEBUG3, "Decided to free page %d\n", p->physical_page);

    /* Okay, found the physical page we want to free for use somewhere
     * else.  Free it. */
    swap_and_free(p);

    status = EOKAY;
out:

    free_list->clock_hand = clock_hand;
    return status;
}


/*!
    Two possibilities:

    (i) page is dirty.  Write it to disk, then free physical memory.

    (ii) page is clean.  Two subcases:

    (a) page that was read out of swap and hasn't been modified
        since that happened

    (b) read-only page read off applications disk that cannot
        be modified

    If (a), then has physical disk location already. Since not
    dirty, we can just discard it without further work.  If
    (b), then we have to write the data to disk, which will
    then give us physical location.

    1st is distinguished by the PTE that references it has a
    disk location.  It may be readable only, or both readable
    and writable.

    2nd is distinguished by being only readable and not having
    a disk location.  After this, 2nd type becomes 1st type (will have
    backing store in the future).  The permissions may be either
    readable || readable/writable.
*/

void
swap_and_free(physical_page_t *p)
{
    if (IS_DIRTY(p->reverse))
    {
        if (p->reverse->disk_location == NOT_ON_DISK)
        {
            swap_out_new(p);
            INCREMENT_STAT(replaced);
        }
        else
        {
            write_out_dirty_page(p);
            INCREMENT_STAT(replaced);
        }
    } else
    {
        if (!p->reverse->disk_location)
            swap_out_new(p);
        /* else:  we're done here.  It's not dirty and it's on the disk */
    }

    /* In any case, we now must give up the memory */
    release_page(p);
}

/*!
    Write out a page that already has an assigned location on the disk.
*/

void
write_out_dirty_page(physical_page_t *p)
{
    int offset,
        access_rights,
        track = 0,
        sector = 0,
        count = 0,
        garbage,
        ret;

    if (!p || !p->reverse)
        KERNEL_ERROR("NULL pointer: page %08x reverse %08x\n",
                     p, (p ? (int)p->reverse: 0x1BADCAFE));

    DP(DEBUG3, "writing out dirty physical page %d\n", p->physical_page);

    /* write it out. To do this, turn 'disk page' info into
     * track/sector info.  We use the reverse mapping to go from the
     * physical page to the process' PTE */
    disk_page_to_geometry(p->reverse->disk_location, &track, &sector, &count);

    DP(DEBUG5, "Creating temporary mapping %d to %d\n",
                TEMP_PAGE, p->physical_page);

    /* Map process' physical page so we can get at the data */
    DOWN(MMU_mutex, garbage);
    ret = MMU_Map(DEFAULT_TAG, TEMP_PAGE, p->physical_page, MMU_PROT_RW);
    if (ret != MMU_OK)
    {
        DP(DEBUG, "Couldn't temp map data: '%s'\n\n", decode_MMU_warning(ret));
        return;
    }

    ret = MMU_GetMap(DEFAULT_TAG, TEMP_PAGE, &offset, &access_rights);
    if (ret != MMU_OK)
    {
        DP(DEBUG, "failed MMU_GetMap: '%s'\n\n", decode_MMU_warning(ret));
        goto out;
    }

    DP(DEBUG5, "Buffering data from physical page starting @ %08x "
               "to %08x (%d bytes) \n"
                "offset %2d pages from base address %08x to kernel buffer @ %08x\n",
               ADDRESS(offset), ADDRESS(offset) + MMU_PageSize(),
               MMU_PageSize(), offset, base_address, disk_buffer);


#if 1
/*
    This works when it should fault, I think.  Unless p->physical_page
    is 0 (which is 1/n where "n" is number of 'frames' allocated), I
    have not.  But no matter what I map, only copying from 0 works.
    But then I think the data will be wrong.  That's what the test
    results seem to be saying.

    I could see that 'offset' could return a pointer to the start of
    the physical address, and I could use that directly, but it
    doesn't.  It returns what looks like a page offset from the start
    of the memory area.  Since I have no other base addresses to which
    to add any '2's or similar numbers that are in 'offset', I add it
    to that, and scale by the page size, since that seems like the
    right thing to do, too.  If only the documentation didn't suck.

    Whatever.  I fucking quit.  Goddamn simulators.

    I can't believe it was less onerous to write an OS in 6800 assembly than
    to do this shit.  Guess I'm a failure and will have to go back to
    hacking on something simple like the Linux kernel.
*/
    memcpy(disk_buffer, ADDRESS(0), MMU_PageSize());
#else
/*
    This doesn't work.

    Map TEMP_PAGE <-> physical_page using MMU_Map()

    Ask for address of this mapping use MMU_GetMap() .  Instead, get
    stupid offset (whose bright idea what that?  What, no one ever
    heard of int **?), scale by page size and add to base address.

    Try to get data from this (presumably now physical) address.

    Kaboom.

    What the hell is it?  It sure seems like
    physical page           physical address
    0               <->     0015a000
    1               <->     0015c000
    2               <->     0015e000
    3               <->     00160000

    And when I map virtual page 0 to physical page ('frame') 1, I
    should be able to access 0015c0000.  Actually, I don't think the
    virtual page should matter at all, except as a convenient spot for
    recording the existance of the map.  I'm copying between two
    physical locations, 'disk_buffer' and 0015c0000.  It's none of
    USLOSS damn business anymore.  I threw it a bone with the MMU_Map,
    get the hell out of my way.

    Screw it.  Porn time.
*/
    memcpy(disk_buffer, ADDRESS(offset), MMU_PageSize());
#endif

    DP(DEBUG5,"Undoing temporary map %d to %d\n", TEMP_PAGE, p->physical_page);

    ret = MMU_Unmap(DEFAULT_TAG, TEMP_PAGE);
    if (ret != MMU_OK)
    {
        DP(DEBUG, "unmapping for temp mapping failed: '%s'\n\n",
                  decode_MMU_warning(ret));
        /* let's see what we can do despite unmapping failure */
    }
    UP(MMU_mutex, garbage);



    DP(DEBUG3, "Writing data to disk @ t %d s %d for %d\n",
               track, sector, count);

    /* write data from process' memory area to disk */
    ret = disk_write_real(SWAP_DISK, track, sector, count, disk_buffer);
    if (ret != EOKAY)
        KERNEL_ERROR("disk write for page %d: track %d sector %d count %d\n",
                     p->reverse->disk_location, track, sector, count);

    /* what's on disk now matches what's in RAM, so no longer dirty */
    MARK_CLEAN(p->reverse);

    INCREMENT_STAT(pageOuts);
    INCREMENT_STAT(faults);
out:
    DP(DEBUG3, "Done writing out dirty page %d\n", p->physical_page);
}



/*!
 *     Initializes all the entries in the process table
 */

void
initialize_process_table(void)
{
    int i = 0;
    for ( ; i < MAXPROC; ++i)
        initialize_process_table_entry(i);
}

/*!
 *     Initializes a single entry in the process table 'index' entries
 *     from the start.
 */

void
initialize_process_table_entry(int index)
{
    int box_ID;
    process_table[index].pid = NOT_A_PID;

    box_ID = MboxCreate(MUTEX_SLOTS, sizeof(int));
    if (box_ID < 0)
        KERNEL_ERROR("Error creating box for process slot %d: %d",index,box_ID);

    process_table[index].box_ID = box_ID;
    process_table[index].table = NULL;
}

/*!
 *     When we're shutting down, release all our mailboxes.  This way,
 *     anyone sleeping on one for whatever reason will wake up.
 */

void
release_process_table_mailboxes(void)
{
    int i = 0, ret;
    DP(DEBUG3, "Releasing process mailboxes\n");
    for ( ; i < MAXPROC; ++i)
    {
        ret = MboxRelease(process_table[i].box_ID);
        if (ret != 0)
            DP(DEBUG, "Error releasing mailbox @ slot %d w/ ID %d \n",
                       i, process_table[i].box_ID);
        DP(DEBUG3, "Releasing mailbox for %d: %d\n", process_table[i].box_ID);
    }
}

