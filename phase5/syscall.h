#ifndef SYSCALL_H
#define SYSCALL_H

#include <phase2.h>             /* sysargs */
#include "types.h"

void vm_init(sysargs *args);
void vm_cleanup(sysargs *args);

int offset_to_page(int offset);

#endif  /* SYSCALL_H */

