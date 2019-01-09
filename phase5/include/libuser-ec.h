
#ifndef _LIBUSER_EC_H
#define _LIBUSER_EC_H
#define EXTRA_CREDIT

/* Phase 5 -- Extra Credit User Function Prototypes */

extern int Protect(int page, int protection);
extern int Share(int pid, int source, int target);
extern int COW(int pid, int source, int target);

#endif
