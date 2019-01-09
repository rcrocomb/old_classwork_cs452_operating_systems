#ifndef HANDLER_H
#define HANDLER_H

struct sysargs;
extern int debugflag2;

void nullsys(sysargs *args);
void clock_handler(int dev, int unit);
void disk_handler(int dev, int unit);
void term_handler(int dev, int unit);
void syscall_handler(int dev, int unit);

#endif  /* HANDLER_H */

