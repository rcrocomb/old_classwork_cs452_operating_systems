/*!
    Author: Robert Crocombe
    Class: CS452 Spring 05
    Assignment: Phase 3
*/

#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>

#include "utility.h"
#include "helper.h"

/* In testcases */
extern int start3(char *arg);

extern int debugflag;
extern int debugflag2;
extern proc_struct_t process_table[];

int debugflag3 = NO_DEBUG;

int
start2(char *arg)
{
    int pid;
    int status;

    debugflag = 0;
    debugflag2 = 0;

    KERNEL_MODE_CHECK;

    /*
     * Data structure initialization as needed...
     */

    initialize_process_table();
    initialize_semaphore_table();
    initialize_sys_vec();

    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * Assumes kernel-mode versions of the system calls
     * with lower-case names.  I.e., Spawn is the user-mode function
     * called by the test cases; spawn is the kernel-mode function that
     * is called by the syscall_handler; spawn_real is the function that
     * contains the implementation and is called by spawn.
     *
     * Spawn() is in libuser.c.  It invokes usyscall()
     * The system call handler calls a function named spawn() -- note lower
     * case -- that extracts the arguments from the sysargs pointer, and
     * checks them for possible errors.  This function then calls spawn_real().
     *
     * Here, we only call spawn_real(), since we are already in kernel mode.
     *
     * spawn_real() will create the process by using a call to fork1 to
     * create a process executing the code in spawn_launch().  spawn_real()
     * and spawn_launch() then coordinate the completion of the phase 3
     * process table entries needed for the new process.  spawn_real() will
     * return to the original caller of Spawn, while spawn_launch() will
     * begin executing the function passed to Spawn. spawn_launch() will
     * need to switch to user-mode before allowing user code to execute.
     * spawn_real() will return to spawn(), which will put the return
     * values back into the sysargs pointer, switch to user-mode, and
     * return to the user code that called Spawn.
     */

    process_table[CURRENT].pid = getpid();
    pid = spawn_real("start3", start3, NULL,  32 * 1024, 3);

    /* call the _real version of your wait code here (may or may not
     * look like my version)
     */
    DP(DEBUG3, "Calling wait_real for 'start3'\n");
    pid = wait_real(&status);

    return pid;
} /* start2 */

