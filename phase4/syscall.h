#ifndef SYSCALL_H
#define SYSCALL_H

#include <phase2.h>

void sleep(sysargs *args);
void disk_read(sysargs *args);
void disk_write(sysargs *args);
void disk_size(sysargs *args);
void term_read(sysargs *args);
void term_write(sysargs *args);

#endif  /* SYSCALL_H */

