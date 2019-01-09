/*
 * Function prototypes from Patrick's phase3 solution. These can be called
 * when in *kernel* mode to get access to phase3 functionality.
 */


#ifndef PROVIDED_PROTOTYPES_H

#define PROVIDED_PROTOTYPES_H

extern int  spawn_real(char *name, int (*func)(char *), char *arg,
                       int stack_size, int priority);
extern int  wait_real(int *status);
extern void terminate_real(int exit_code);
extern int  semcreate_real(int init_value);
extern int  semp_real(int semaphore);
extern int  semv_real(int semaphore);
extern int  semfree_real(int semaphore);
extern int  gettimeofday_real(int *time);
extern int  cputime_real(int *time);
extern int  getPID_real(int *pid);

/*
 * Function prototypes from Patrick's phase4 solution. These can be called
 * when in *kernel* mode to get access to phase4 functionality.
 */

extern int sleep_real(int seconds);
extern int disk_read_real (int unit, int track, int first_sector,
                           int num_sectors, void *buffer);
extern int disk_write_real(int unit, int track, int first_sector,
                           int num_sectors, void *buffer);
extern int disk_size_real (int unit, int *sectorSize, int *trackSize,
                           int *diskSize);

#endif  /* PROVIDED_PROTOTYPES_H */
