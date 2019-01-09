#ifndef _SYSCALL_H
#define _SYSCALL_H

/*  Different possible syscall opcodes */

/* For Phase 3 */
#define SYS_SPAWN		1
#define SYS_WAIT		2
#define SYS_TERMINATE		3
#define SYS_SEMCREATE		4
#define SYS_SEMP		5
#define SYS_SEMV		6
#define SYS_SEMFREE		7
#define SYS_GETTIMEOFDAY	8
#define SYS_CPUTIME		9
#define SYS_GETPID		10

/* For Phase 4 */
#define SYS_SLEEP               11
#define SYS_DISKREAD            12
#define SYS_DISKWRITE           13
#define SYS_DISKSIZE            14
#define SYS_TERMREAD            15
#define SYS_TERMWRITE           16

/* Interface to Phase 2 mailbox routines -- needed by some test cases */
#define SYS_MBOXCREATE          17
#define SYS_MBOXRELEASE         18
#define SYS_MBOXSEND            19
#define SYS_MBOXRECEIVE         20
#define SYS_MBOXCONDSEND        21
#define SYS_MBOXCONDRECEIVE     22

/* For Phase 5 */
#define SYS_VMINIT              23
#define SYS_VMCLEANUP           24

#ifdef EXTRA_CREDIT

/* The following are for the extra credit in Phase 5 */
#define SYS_PROTECT             25
#define SYS_SHARE               26
#define SYS_COW                 27
#define NUM_SYSCALLS            28

#else

#define NUM_SYSCALLS		25

#endif


/*  The sysargs structure */
/* -- Now defined in phase2.h, not here...
typedef struct sysargs
{
	int number;
	void *arg1;
	void *arg2;
	void *arg3;
	void *arg4;
	void *arg5;
} sysargs;
*/

extern void usyscall(sysargs *sa);

#endif	/*  _SYSCALL_H */

