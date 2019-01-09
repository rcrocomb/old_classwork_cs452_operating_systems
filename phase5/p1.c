/*!
    Author: Robert Crocombe
    Class: CS452 Operating Systems Spring 2005
    Professor: Patrick Home

    These are the routines that initiate just about all normal VM
    activity (activity not caused by a fault).
*/

#include <phase1.h>         /* prototypes are here */
#include <phase5.h>         /* MMU_* */

#include "types.h"
#include "utility.h"
#include "syscall.h"
#include "vm_core.h"

/******************************************************************************/
/* Globals                                                                    */
/******************************************************************************/

extern proc_table_t process_table[];
extern VmStats vmStats;
extern int MMU_mutex;

/******************************************************************************/
/* Internal Prototypes                                                        */
/******************************************************************************/

static void unmap_table(int pid);
static void map_table(int pid);

/******************************************************************************/
/* Definitions                                                                */
/******************************************************************************/

/*!
    Create page table for process with pid 'pid', assuming that VM
    system has been started.  Else return without having done anything.
*/

void
p1_fork(int pid)
{
    int pages;

    KERNEL_MODE_CHECK;

    /* has VM stuff been initialized?  If not, return */
    if (!MMU_Region(&pages))
    {
        DP_NOPID(DEBUG5, "fork for %d before VM initialized\n", pid);
        return;
    }

    DP(DEBUG4, "fork for %d\n", pid);
    initialize_page_table(pid, vmStats.pages);
}

/*!
    Unload the existing page map for process with pid 'old'.  Load the
    page map for the process with pid 'new'.
*/

void
p1_switch(int old, int new)
{
    int pages;

    KERNEL_MODE_CHECK;
    INCREMENT_STAT(switches);

    /* has VM stuff been initialized?  If not, return */
    if (!MMU_Region(&pages))
    {
        DP_NOPID(DEBUG5, "switch from %d to %d before VM init\n", old, new);
        return;
    }

    DP(DEBUG4, "switch from %d to %d\n", old, new);
    unmap_table(old);
    map_table(new);
}

/*!
    Release a process' page table.
*/

void
p1_quit(int pid)
{
    int pages;

    KERNEL_MODE_CHECK;

    /* has VM stuff been initialized?  If not, return */
    if (!MMU_Region(&pages))
    {
        DP_NOPID(DEBUG5, "quit for process %d before VM init\n", pid);
        return;
    }

    DP(DEBUG4, "Releasing mappings for process %d\n", pid);
    unmap_table(pid);
    SMART_FREE(process_table[GET_SLOT(pid)].table);
}

/*!
    When process is switched out, release an MMU mappings so the next
    guy can use the MMU.
*/

void
unmap_table(int pid)
{
    proc_table_t *process = &(process_table[GET_SLOT(pid)]);
    PTE_t *table = process->table;
    PTE_t *entry;
    int garbage, ret, i = 0, pages_count = 0;

    if (!table)
    {
        DP(DEBUG3, "Process %d has no page table\n", pid);
        return;
    }


    /* unload existing mappings */
    for (i = 0; i < vmStats.pages; ++i)
    {
        entry = &(process->table[i]);
        if (!entry)
            KERNEL_ERROR("Ugly error unmapping PTE %d for process %d\n", i,pid);

        if (entry->mapped_to)
        {
            DOWN(MMU_mutex, garbage);
            ret = MMU_Unmap(DEFAULT_TAG, i);
            UP(MMU_mutex, garbage);
            if (ret != MMU_OK)
            {
                DP(DEBUG, "unmapping process %d: failure to unmap %08x: %s\n",
                      pid, i, decode_MMU_warning(ret));
            }
            DP(DEBUG4, "Unmapped pid %d entry %d: %d to %d\n",
                       pid, i, i, entry->mapped_to->physical_page);
            ++pages_count;
        }
        /* else it's swapped out and doesn't need to be unmapped */
    }

    DP(DEBUG3, "Unmapped pid %d pages %d of %d\n",
               pid, pages_count, vmStats.pages);
}

/*!
    Load any MMU mappings.  We'll have some PTEs that aren't
    MMU-applicable: if the data is swapped to disk, for instance.
*/

void
map_table(int pid)
{
    proc_table_t *process = &(process_table[GET_SLOT(pid)]);
    PTE_t *table = process->table;
    PTE_t *entry;
    int ret, garbage, i = 0, pages_count = 0;

    if (!table)
    {
        DP(DEBUG3, "Process %d has no page table\n", pid);
        return;
    }

    /* For each PTE entry for this process */
    for (i = 0; i < vmStats.pages; ++i)
    {
        entry = &(process->table[i]);
        if (!entry)
            KERNEL_ERROR("Ugly error mapping PTE %d for process %d\n", i, pid);

        /* Only virtual pages that are associated with physical pages
           need to be mapped: no need to map pages that are swapped out, for
           instance.*/
        if (entry->mapped_to)
        {
            DOWN(MMU_mutex, garbage);
            ret = MMU_Map(DEFAULT_TAG,
                          i,
                          entry->mapped_to->physical_page,
                          MMU_PROT_RW);
            UP(MMU_mutex, garbage);
            if (ret != MMU_OK)
            {
                DP(DEBUG, "Error mapping for pid %d vpage %d to ppage %d: %s\n",
                          pid,
                          i,
                          entry->mapped_to->physical_page,
                          decode_MMU_warning(ret));
            }
            DP(DEBUG4, "Mapped pid %d entry %d: %d to %d\n",
                       pid, i, i, entry->mapped_to->physical_page);
            ++pages_count;
        } else
            DP(DEBUG4, "Not mapping pid %d entry %d\n", pid, i);
    }

    DP(DEBUG3, "Mapped pid %d pages %d of %d\n",
               pid, pages_count, vmStats.pages);
}

