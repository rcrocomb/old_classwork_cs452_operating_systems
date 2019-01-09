/*
 * skeleton.c
 *
 * This is a skeleton for phase5 of the programming assignment. It
 * doesn't do much -- it is just intended to get you started.
 */


#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase5.h>
#include <usyscall.h>
#include <libuser.h>
#include <vm.h>
#include <string.h>

extern void mbox_create(sysargs *args_ptr);
extern void mbox_release(sysargs *args_ptr);
extern void mbox_send(sysargs *args_ptr);
extern void mbox_receive(sysargs *args_ptr);
extern void mbox_condsend(sysargs *args_ptr);
extern void mbox_condreceive(sysargs *args_ptr);

static Process processes[MAXPROC];

FaultMsg faults[MAXPROC]; /* Note that a process can have only
                           * one fault at a time, so we can
                           * allocate the messages statically
                           * and index them by pid. */
VmStats  vmStats;


static void FaultHandler(int type, int offset);

static void vm_init(sysargs *sysargsPtr);
static void vm_cleanup(sysargs *sysargsPtr);
/*
 *----------------------------------------------------------------------
 *
 * start4 --
 *
 * Initializes the VM system call handlers.
 *
 * Results:
 *      MMU return status
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
int
start4(char *arg)
{
   int pid;
   int result;
   int status;

   /* to get user-process access to mailbox functions */
   sys_vec[SYS_MBOXCREATE]      = mbox_create;
   sys_vec[SYS_MBOXRELEASE]     = mbox_release;
   sys_vec[SYS_MBOXSEND]        = mbox_send;
   sys_vec[SYS_MBOXRECEIVE]     = mbox_receive;
   sys_vec[SYS_MBOXCONDSEND]    = mbox_condsend;
   sys_vec[SYS_MBOXCONDRECEIVE] = mbox_condreceive;

   /* user-process access to VM functions */
   sys_vec[SYS_VMINIT]    = vm_init;
   sys_vec[SYS_VMCLEANUP] = vm_cleanup;

   result = Spawn("Start5", start5, NULL, 8*USLOSS_MIN_STACK, 2, &pid);
   if (result != 0) {
      console("start4(): Error spawning start5\n");
      Terminate(1);
   }
   result = Wait(&pid, &status);
   if (result != 0) {
      console("start4(): Error waiting for start5\n");
      Terminate(1);
   }
   Terminate(0);
   return 0; // not reached

} /* start4 */

/*
 *----------------------------------------------------------------------
 *
 * VmInit --
 *
 * Stub for the VmInit system call.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      VM system is initialized.
 *
 *----------------------------------------------------------------------
 */
static void
vm_init(sysargs *sysargsPtr)
{
   CheckMode();
} /* vm_init */


/*
 *----------------------------------------------------------------------
 *
 * vm_cleanup --
 *
 * Stub for the VmCleanup system call.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      VM system is cleaned up.
 *
 *----------------------------------------------------------------------
 */

static void
vm_cleanup(sysargs *sysargsPtr)
{
   CheckMode();
} /* vm_cleanup */


/*
 *----------------------------------------------------------------------
 *
 * vm_init_real --
 *
 * Called by vm_init.
 * Initializes the VM system by configuring the MMU and setting
 * up the page tables.
 *
 * Results:
 *      Address of the VM region.
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
void *
vm_init_real(int mappings, int pages, int frames, int pagers)
{
   int status;
   int dummy;

   CheckMode();
   status = MMU_Init(mappings, pages, frames);
   if (status != MMU_OK) {
      console("vm_init: couldn't initialize MMU, status %d\n", status);
      abort();
   }
   int_vec[MMU_INT] = FaultHandler;

   /*
    * Initialize page tables.
    */

   /*
    * Create the fault mailbox.
    */

   /*
    * Fork the pagers.
    */

   /*
    * Zero out, then initialize, the vmStats structure
    */
   memset((char *) &vmStats, 0, sizeof(VmStats));
   vmStats.pages = pages;
   vmStats.frames = frames;
   /*
    * Initialize other vmStats fields.
    */

   return MMU_Region(&dummy);
} /* vm_init_real */


/*
 *----------------------------------------------------------------------
 *
 * PrintStats --
 *
 *      Print out VM statistics.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Stuff is printed to the console.
 *
 *----------------------------------------------------------------------
 */
void
PrintStats(void)
{
    console("VmStats\n");
    console("pages:\t\t%d\n", vmStats.pages);
    console("frames:\t\t%d\n", vmStats.frames);
    console("blocks:\t\t%d\n", vmStats.blocks);
    console("freeFrames:\t%d\n", vmStats.freeFrames);
    console("freeBlocks:\t%d\n", vmStats.freeBlocks);
    console("switches:\t%d\n", vmStats.switches);
    console("faults:\t\t%d\n", vmStats.faults);
    console("new:\t\t%d\n", vmStats.new);
    console("pageIns:\t%d\n", vmStats.pageIns);
    console("pageOuts:\t%d\n", vmStats.pageOuts);
    console("replaced:\t%d\n", vmStats.replaced);
} /* PrintStats */


/*
 *----------------------------------------------------------------------
 *
 * vm_cleanup_real --
 *
 * Called by vm_clean.
 * Frees all of the global data structures
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The MMU is turned off.
 *
 *----------------------------------------------------------------------
 */
void
vm_cleanup_real(void)
{

   CheckMode();
   MMU_Done();
   /*
    * Kill the pagers here.
    */
   /*
    * Print vm statistics.
    */
   console("vmStats:\n");
   console("pages: %d\n", vmStats.pages);
   console("frames: %d\n", vmStats.frames);
   console("blocks: %d\n", vmStats.blocks);
   /* and so on... */

} /* vm_cleanup_real */

/*
 *----------------------------------------------------------------------
 *
 * FaultHandler
 *
 * Handles an MMU interrupt. Simply stores information about the
 * fault in a queue, wakes a waiting pager, and blocks until
 * the fault has been handled.
 *
 * Results:
 * None.
 *
 * Side effects:
 * The current process is blocked until the fault is handled.
 *
 *----------------------------------------------------------------------
 */
static void
FaultHandler(int type /* MMU_INT */,
             int arg  /* Offset within VM region */)
{
   int cause;

   assert(type == MMU_INT);
   cause = MMU_GetCause();
   assert(cause == MMU_FAULT);
   vmStats.faults++;
   /*
    * Fill in faults[pid % MAXPROC], send it to the pagers, and wait for the
    * reply.
    */
} /* FaultHandler */


/*
 *----------------------------------------------------------------------
 *
 * Pager
 *
 * Kernel process that handles page faults and does page replacement.
 *
 * Results:
 * None.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */
static int
Pager(char *buf)
{
   while(1) {
      /* Wait for fault to occur (receive from mailbox) */
      /* Look for free frame */
      /* If there isn't one then use clock algorithm to
       * replace a page (perhaps write to disk) */
      /* Load page into frame from disk, if necessary */
      /* Unblock waiting (faulting) process */
   }
   return 0;
} /* Pager */
