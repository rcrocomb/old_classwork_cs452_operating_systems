/*
 *  User-visible definitions for usloss. Users of USLOSS are probably
 *  interested in everything in here.
 */

#if !defined(_usloss_h)
#define _usloss_h

#include <stdarg.h>

/*  We need machine.h for the context type */
#ifdef sun
#include "solaris/machine.h"
#endif
#ifdef linux
#include "linux/machine.h"
#endif
#ifdef MMU
#include "mmu.h"
#endif

/*  Function prototypes for USLOSS functions */
extern int		device_input(unsigned int dev, int unit, int *status);
extern int		device_output(unsigned int dev, int unit, void *arg);
extern void		waitint(void);
extern void		halt(int dumpcore);
extern void		console(char *string, ...);
extern void		vconsole(char *string, va_list ap);
extern void		trace(char *string, ...);
extern void		vtrace(char *string, va_list ap);
extern void		context_init(context *state, unsigned int psr,
			    char *stack, void (*func)(void));
extern void		context_switch(context *old, context *new);
extern unsigned int	psr_get(void);
extern void		psr_set(unsigned int psr);
extern int		sys_clock(void);

/*
 *  This tells how many slots are in the intvec
 *  NUM_INTS = number of device types +  1 (for syscall interrupt)
 */
#define NUM_INTS	6	/* number of interrupts */

/*
 *  This is the interrupt vector table
 */
extern void (*int_vec[NUM_INTS])(int dev, int unit);

/*
 *  These are the defined values for the individual devices
 *  in the intvec
 */
#define CLOCK_DEV	0	/* clock */
#define ALARM_DEV	1	/* alarm */
#define DISK_DEV	2	/* disk */
#define TERM_DEV	3	/* terminal */
#define MMU_INT		4	/* MMU */
#define SYS_INT		5	/* syscall */
#define SYSCALL         SYS_INT

#define LOW_PRI_DEV	TERM_DEV  /* terminal is lowest priority */


/*
 * # of units of each device type
 */

#define CLOCK_UNITS	1
#define ALARM_UNITS	1
#define DISK_UNITS	2
#define TERM_UNITS	4
/*
 * Maximum number of units of any device.
 */

#define MAX_UNITS	4

/*
 *  This is the structure used to send a request to
 *  a device.
 */
typedef struct
{
	int opr;
	void *reg1;
	void *reg2;
} device_request;

/*
 *  These are the operations for the disk device
 */
#define DISK_READ	0
#define DISK_WRITE	1
#define DISK_SEEK	2
#define DISK_TRACKS	3

/*
 *  These are the status codes returned by device_input(). In general,
 *  the status code is in the lower byte of the int returned; the upper
 *  bytes may contain other info. See the documentation for the
 *  specific device for details.
 */
#define DEV_READY	0
#define DEV_BUSY	1
#define DEV_ERROR	2

/*
 * device_output() and device_input() will return DEV_OK if their
 * arguments were valid, DEV_INVALID otherwise. By valid, the device
 * type and unit must correspond to a device that exists.
 */

#define DEV_OK		0
#define DEV_INVALID	1

/*
 * These are the fields of the terminal status registers. A call to
 * device_input will return the status register, and you can use these
 * macros to extract the fields. The xmit and recv fields contain the
 * status codes listed above.
 */

#define TERM_STAT_CHAR(status)\
	(((status) >> 8) & 0xff)	/* character received, if any */

#define	TERM_STAT_XMIT(status)\
	(((status) >> 2) & 0x3) 	/* xmit status for unit */

#define	TERM_STAT_RECV(status)\
	((status) & 0x3)		/* recv status for unit */

/*
 * These are the fields of the terminal control registers. You can use
 * these macros to put together a control word to write to the
 * control registers via device_output.
 */

#define TERM_CTRL_CHAR(ctrl, ch)\
	((ctrl) | (((ch) & 0xff) << 8))/* char to send, if any */

#define	TERM_CTRL_XMIT_INT(ctrl)\
	((ctrl) | 0x4)			/* enable xmit interrupts */

#define	TERM_CTRL_RECV_INT(ctrl)\
	((ctrl) | 0x2)			/* enable recv interrupts */

#define TERM_CTRL_XMIT_CHAR(ctrl)\
	((ctrl) | 0x1)			/* xmit the char in the upper bits */


/*
 *  Size of disk sector (in bytes) and number of sectors in a track
 */
#define DISK_SECTOR_SIZE	512
#define DISK_TRACK_SIZE		16

/*
 * Processor status word (PSR) fields. Current is the current mode
 * and interrupt values, prev are the values prior to the last
 * interrupt. The interrupt handler moves current into prev on an
 * interrupt, and restores current from prev on returning.
 */

#define PSR_CURRENT_MODE 	0x1
#define PSR_CURRENT_INT		0x2
#define PSR_PREV_MODE		0x4
#define PSR_PREV_INT		0x8

#define PSR_CURRENT_MASK	0x3
#define PSR_PREV_MASK		0xc
#define PSR_MASK 		(PSR_CURRENT_MASK | PSR_PREV_MASK)

/*
 * Length of a clock tick.
 */

#define CLOCK_MS	20

/*
 * Minimum stack size.
 */

#define USLOSS_MIN_STACK 8192

/* the highest priority a process can have */
#define HIGHEST_PRIORITY 1
/* the lowest priority a process can have */
#define LOWEST_PRIORITY 6

#endif	/*  _usloss_h */

