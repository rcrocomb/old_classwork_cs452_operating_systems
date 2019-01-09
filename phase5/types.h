#ifndef TYPES_H
#define TYPES_H

/*!
    Author: Robert Crocombe
    Class: CS452 Spring 05
    Professor: Patrick Homer

    Types and some macros to go with them.

    I've never really used flexible arrays before, so I thought I'd
    throw that in this time.
*/

/******************************************************************************/
/* Flags that indicate permissions and behavior of page data                  */
/******************************************************************************/

/*!
Wed May  4 00:32:51 MST 2005
Some of this might even be correct:

        READABLE    WRITABLE
            0           0       Page cannot be accessed at all  MMU_PROT_NONE
            0           1       Invalid permissions combination
            1           0       Read-only data                  MMU_PROT_READ
            1           1       readable and writeable data     MMU_PROT_RW

    If SWAPPED_OUT is set, then the data that is referenced by that
    PTE is on disk and not in physical RAM.  To access the data, this
    will have to be rectified.  There will be no MMU mapping for this entry.

    If VALID is not set, then the PTE is one that has yet to be
    initialized: the mapping has been made, but the requisite data
    isn't loaded.  There will be no MMU mapping in this case.

        SWAPPED_OUT     VALID
            0               0   Brand new frame that is yet to be set
            0               1   An MMU entry should be made (data is OK)
            1               0   Invalid flag combination
            1               1   Data has been swapped out.

    REFERENCED: set once data has been accessed in any way.  This is
    for use with the page replacement algorithm.

    DIRTY: set when data has been modified.  An indicator to write
    data to disk if this page is booted from the MMU (by the page
    replacement algorithm).

        REFERENCED      DIRTY
            0               0   Untouched.  Good candidate for unmapping.
            0               1   I think this can happen when clock is looping
            1               0   Page has been read.
            1               1   Page has ben written (and maybe read as well).

*/

/* flags */
#define READABLE    (1 << 0)        /* permissions allow mem to be read */
#define WRITABLE    (1 << 1)        /* permissions allow mem to be written */
#define PRESENT     (1 << 3)        /* data is physically in RAM */
#define REFERENCED  (1 << 30)       /* data has been read or written */
#define DIRTY       (1 << 31)       /* data in RAM doesn't match disk data */


/******************************************************************************/
/* Macros for the above flags                                                 */
/******************************************************************************/

/* Macros all accept a pointer to either PTE_t or physical_page_t
 * It's a little shady, since it only works because variable 'flags' is the
 * same for both types.  However, if you change one of them, then the
 * macro will explode during compilation, so I figure it's not all
 * bad. */

#define CLEAR_ALL_FLAGS(p)      ((p)->flags = 0)

#define IS_READABLE(p)          ((p)->flags & READABLE)
#define IS_WRITABLE(p)          ((p)->flags & WRITABLE)
#define IS_PRESENT(p)           ((p)->flags & PRESENT)
#define IS_REFERENCED(p)        ((p)->flags & REFERENCED)
#define IS_DIRTY(p)             ((p)->flags & DIRTY)

#define MARK_READABLE(p)        ((p)->flags |= READABLE)
#define CLEAR_READABLE(p)       ((p)->flags &= ~READABLE)
#define MARK_WRITABLE(p)        ((p)->flags |= WRITABLE)
#define CLEAR_WRITABLE(p)       ((p)->flags &= ~WRITABLE)
#define MARK_PRESENT(p)         ((p)->flags |= PRESENT)
#define CLEAR_PRESENT(p)        ((p)->flags &= ~PRESENT)
#define MARK_NOT_PRESENT(p)     CLEAR_PRESENT(p)

#define MARK_READ(p)            ((p)->flags |= REFERENCED)
#define MARK_WROTE(p)           ((p)->flags |= (REFERENCED | DIRTY))
#define MARK_UNREFERENCED(p)    ((p)->flags &= ~REFERENCED)
#define MARK_CLEAN(p)           ((p)->flags &= ~DIRTY)

#define IS_FREE(p)              ((p)->is_free)
#define MARK_FREE(p)            ((p)->is_free = 1)
#define MARK_NOT_FREE(p)        ((p)->is_free = 0)
/******************************************************************************/
/* Various other Macros                                                       */
/******************************************************************************/

#define PAGE_BLOCK_SIZE         (sizeof(page_ID_t) * BITSPERBYTE)
#define SMART_FREE(a)           if(a) free(a); a = NULL

/******************************************************************************/
/* Types                                                                      */
/******************************************************************************/

typedef unsigned char byte;

/*
    One PTE_t for each virtual page in use by a process.

    If the virtual page has a corresponding physical page (i.e., isn't
    swapped out), then mapped_to will point to the physical page
    structure to which the virtual page is mapped.

    If the page is swapped out (mapped_to is NULL), then the
    disk_location field will have the track on the swap disk where the
    data can be found.

    The 'next' field points to the next virtual page entry for this process.
*/

typedef struct _page_table_entry
{
    struct _physical_page_struct *mapped_to;
    int disk_location;
    int flags;

    struct _page_table_entry *next;

} PTE_t;

/*!
    The physical page is the page number

    flags has information about the state of the memory: whether the
    page is dirty and needs to be written to disk if page is reused.

    'next' points to the next free memory page, or NULL if this page
    is used.  Check the 'FREE' bit to find out.  This bit is necessary
    because the last entry in the list of free physical pages will
    also have its next pointer set to NULL.

     'reverse' points back to entry that is mapping us.  Very usable
     when having to swap data out.  We scan through list of physical
     pages and find good candidate.  Then we find who has mapped that
     page and alter their info so we can use the page for someone
     else.  Otherwise we'd have to scan everyone's mappings.
*/

typedef struct _physical_page_struct
{
    int physical_page;
    int is_free;
    struct _physical_page_struct *next;
    PTE_t *reverse;
} physical_page_t;

/*!
    There is a process table entry for each process.

    The 'pid' is the process ID of the process using the entry.

    'map_front' and 'map_back' are pointers to the front and back,
    respectively, of a linked list where each entry is a virtual <->
    physical translation of one of the process' pages.
*/

typedef struct _proc_table_entry
{
    int pid;
    int box_ID;
    PTE_t *table;
} proc_table_t;

/*!
    Maintains a list of the physical memory pages.  Memory that
    is freed goes on the front of the list, so the same memory will be
    used over and over again.

    The mutex provides mutual exclusion.  Shock.
*/

typedef struct _page_list_struct
{
    int mutex_ID;
    int page_count;
    int clock_hand;
    physical_page_t *free_head;
    physical_page_t pages[];
} page_list_t;

/*!
    pid: process ID of process causing the fault.
    cause: one of MMU_FAULT, MMU_ACCESS, MMU_NONE
    offset: offset from start of VM region (in bytes) where
    fault was generated

    You know, I could use the process' box ID instead of sending over
    the 'whole' pid, but I've gotten that paradigm in my head somehow.
    Eh: same size.
*/

typedef struct _MMU_handler_struct
{
    int pid;
    int cause;
    int offset;
} MMU_fault_t;

/*!

*/

typedef struct _swap_disk_struct
{
    /* parameters it's handy to have around. */
    int page_size;          /* in bytes                                 */
    int disk_bytes;         /* bytes on this disk                       */
    int disk_pages;         /* # pages that will fit on the disk        */
    int sectors_per_page;   /* # sectors worth of bytes to hold a page  */
    int sector_size;        /* in bytes                                 */
    int track_size;         /* in sectors                               */
    int tracks_count;       /* # of tracks on disk # SWAP_DISK          */
    int usage[];
} swap_disk_t;

#endif  /* TYPES_H */

