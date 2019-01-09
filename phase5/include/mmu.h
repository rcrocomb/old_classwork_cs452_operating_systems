/*
 * mmu.h
 *
 *	External declarations for the usloss mmu.
 */
#ifndef _MMU_H
#define _MMU_H
#include <sys/unistd.h>

#define MMU_NUM_TAG	4	/* Maximum number of tags in MMU */

/*
 * Error codes
 */
#define MMU_OK		0	/* Everything hunky-dory */
#define MMU_ERR_OFF	1	/* MMU not enabled */
#define MMU_ERR_ON	2	/* MMU already initialized */
#define MMU_ERR_PAGE	3	/* Invalid page number */
#define MMU_ERR_FRAME	4	/* Invalid frame number */
#define MMU_ERR_PROT	5	/* Invalid protection */
#define MMU_ERR_TAG	6	/* Invalid tag */
#define MMU_ERR_REMAP	7	/* Page already mapped */
#define MMU_ERR_NOMAP	8	/* Page not mapped */
#define MMU_ERR_ACC	9	/* Invalid access bits */
#define MMU_ERR_MAPS	10	/* Too many mappings */

/*
 * Protections
 */
#define MMU_PROT_NONE	0	/* Page cannot be accessed */
#define MMU_PROT_READ	1	/* Page is read-only */
#define MMU_PROT_RW	3	/* Page can be both read and written */

/*
 * Causes
 */
#define MMU_FAULT	1	/* Address was in unmapped page */
#define MMU_ACCESS	2	/* Access type not permitted on page */

/*
 * Access bits
 */
#define MMU_REF		1	/* Page has been referenced */
#define MMU_DIRTY	2	/* Page has been written */

/*
 * Function prototypes for MMU routines. See the MMU documentation.
 */

extern int 	MMU_Init(int numMaps, int numPages, int numFrames);
extern void	*MMU_Region(int *numPagesPtr);
extern int	MMU_Done(void);
extern int	MMU_Map(int tag, int page, int frame, int protection);
extern int	MMU_Unmap(int tag, int page);
extern int	MMU_GetMap(int tag, int page, int *framePtr, int *protPtr);
extern int	MMU_GetCause(void);
extern int	MMU_SetAccess(int frame, int access);
extern int	MMU_GetAccess(int frame, int *accessPtr);
extern int	MMU_SetTag(int tag);
extern int	MMU_GetTag(int *tagPtr);
extern int	MMU_PageSize(void);
extern int	MMU_Touch(void *addr);

#endif
