#ifndef HELPER_H
#define HELPER_H

/*!
 *     Author: Robert Crocombe
 *     Class: CS452 Operating Systems Spring 2005
 *     Professor: Patrick Homer
 *
 *     Little helper routines that aren't an integral part of
 *     functionality, really, but are useful: disk maintenance and
 *     mutex get/release routines.
 */

#include "types.h"

/*
I thought "C" had forward declarations?

struct proc_table_entry;
struct disk_info_t;
*/

void add_to_expiry_list(proc_table_entry *entry);
proc_table_entry *remove_from_expiry_list(proc_table_entry *entry);
void break_expiry_list(proc_table_entry *p, proc_table_entry *previous);

void add_to_disk_list(proc_table_entry *entry, int unit);
proc_table_entry *remove_from_disk_list(proc_table_entry *entry, int unit);
void break_disk_list(proc_table_entry *p,
                     proc_table_entry *previous,
                     disk_info_t *disk);

int  get_mutex(int mutex_ID);
int  release_mutex(int mutex_ID);

#endif  /* HELPER_H */

