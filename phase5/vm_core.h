#ifndef VM_CORE_H
#define VM_CORE_H

#include <signal.h>
#include <phase5.h>
#include "types.h"
#include "utility.h"

extern VmStats vmStats;
extern volatile sig_atomic_t stat_mutex;

int initialize_page_list(int physical_pages);
void initialize_swap_disk(void);
void initialize_page_table(int pid, int table_size);
physical_page_t *find_free_page(void);
void release_page(physical_page_t *to_free);

void release_paging_disk_allocations(void);
void release_process_table_mailboxes(void);
void mark_disk_page_clear(int page);
void disk_page_to_geometry(int disk_page, int *t, int *s, int *c);

/* mutual exclusion. */
#define INCREMENT_STAT(field) do { \
                                    DP(DEBUG6, "%2d Incrementing '%s'\n", stat_mutex, #field);\
                                    while (stat_mutex == 0) /* spin */; \
                                    stat_mutex = 0; \
                                    vmStats.field += 1; \
                                    stat_mutex = 1; \
                                    DP(DEBUG6, "%2d DONE Incrementing '%s'\n", stat_mutex, #field);\
                              } while (0)

#define DECREMENT_STAT(field) do { \
                                    DP(DEBUG6, "%2d Decrementing '%s'\n", stat_mutex, #field);\
                                    while (stat_mutex == 0) /* spin */; \
                                    stat_mutex = 0; \
                                    vmStats.field -= 1; \
                                    stat_mutex = 1; \
                                    DP(DEBUG6, "%2d DONE Decrementing '%s'\n", stat_mutex, #field);\
                              } while (0)

#endif  /*  VM_CORE_H */
