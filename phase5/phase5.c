/*!
    Author: Robert Crocombe
    Class: CS452 Spring 05
    Professor: Patrick Homer

    Not much, really.  Starts 'start5' which does actual work.  I
    probably wouldn't have even done the SYS_ stuff if it hadn't been
    in the skeleton.
*/

#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase5.h>
#include <usyscall.h>
#include <libuser.h>

#include "syscall.h"
#include "types.h"
#include "utility.h"

#include <signal.h>

/******************************************************************************/
/* Macros                                                                     */
/******************************************************************************/

#define START5_PRIO 2
#define START5_STACK (8 * USLOSS_MIN_STACK)

/******************************************************************************/
/* Globals                                                                    */
/******************************************************************************/

extern void (*sys_vec[])(sysargs *args);

proc_table_t process_table[MAXPROC];
page_list_t *free_list;
byte *disk_buffer;
int fault_handler_queue;
swap_disk_t *swap_disk;
int pager_daemon_pids[MAXPAGERS];
unsigned char *base_address;   /* Oh */
int MMU_mutex;                  /* good grief */

/* this name is magical: do not change */
VmStats vmStats;
volatile sig_atomic_t stat_mutex = 1;

int debugflag5 = DEBUG5;

/******************************************************************************/
/* Prototypes for external functions                                          */
/******************************************************************************/

extern void mbox_create(sysargs *args);
extern void mbox_release(sysargs *args);
extern void mbox_send(sysargs *args);
extern void mbox_receive(sysargs *args);
extern void mbox_condsend(sysargs *args);
extern void mbox_condreceive(sysargs *args);

extern int start5(char *arg);

/******************************************************************************/
/* Definitions                                                                */
/******************************************************************************/

/*!

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

    result = Spawn("start5", start5, NULL, START5_STACK, START5_PRIO, &pid);
    if (result != 0)
    {
        KERNEL_WARNING("error spawning start5\n");
        Terminate(1);
    }

    DP(DEBUG5, "Waiting for start5 (pid %d) to terminate\n", pid);

    result = Wait(&pid, &status);
    if (result != 0)
    {
        KERNEL_WARNING("error waiting for start5\n");
        Terminate(1);
    }

    Terminate(0);
    return 0; // not reached
}

