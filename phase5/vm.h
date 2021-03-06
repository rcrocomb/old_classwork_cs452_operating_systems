/*
 * vm.h
 */


/*
 * All processes use the same tag.
 */
#define TAG 0

/*
 * Different states for a page.
 */

#define UNUSED 0
#define INCORE 1
/* You'll probably want more states */


/*
 * Page table entry.
 */

typedef struct PTE {
    int  state;  /* See above. */
    int  frame;  /* Frame that stores the page (if any). */
    int  block;  /* Disk block that stores the page (if any). */
    /* Add more stuff here */
} PTE;

/*
 * Per-process information.
 */
typedef struct Process {
    int  numPages;   /* Size of the page table. */
    PTE  *pageTable; /* The page table for the process. */
    /* Add more stuff here */
} Process;

/*
 * Information about page faults. This message is sent by the faulting
 * process to the pager to request that the fault be handled.
 */
typedef struct FaultMsg {
    int  pid;        /* Process with the problem. */
    void *addr;      /* Address that caused the fault. */
    int  replyMbox;  /* Mailbox to send reply. */
    /* Add more stuff here. */
} FaultMsg;

#define CheckMode() assert(psr_get() & PSR_CURRENT_MODE)
