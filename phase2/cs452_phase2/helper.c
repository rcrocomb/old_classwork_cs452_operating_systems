#include "helper.h"
#include "utility.h"
#include "handler.h"

#include <phase2.h>
#include <phase1.h>

/******************************************************************************/
/* Global Variables                                                           */
/******************************************************************************/

extern int next_box_ID;
extern int slots_in_use;
extern int boxes_in_use;
extern mail_box MailBoxTable[];
extern mail_slot message_slots[];
extern int device_mbox_ID[];
extern proc_entry process_table[];

extern int debugflag2;

/*
    Assuming MAXMBOX < MAXINT, then there should always be a free ID
    somewhere, but it's possible that if there's a lot of mailbox churn,
    but also a stable mailbox used by long-lasting process, then we could
    wrap around the list and try to use the same ID twice.  This should
    prevent that.
*/

int
get_next_ID(void)
{
    int i;
    int potential = next_box_ID + 1;
    /* do not want a mbox with ID of 0: use it to identify empty table slots */
    potential += potential == 0;

    do 
    {
        for ( i = 0; i < MAXMBOX; ++i)
            /* check for dupe.  If dupe, then must try next possible ID */
            if (MailBoxTable[i].mbox_ID == potential)
                break;

        /* If dupe, try next ID. */
        if (i < MAXMBOX)
            ++potential;

      /* Stop when (a) have made it through entire list with no matches (GOOD)
         (b) have completely wrapped around list of IDs (BAD)  */
    } while ((i < MAXMBOX) && (potential != next_box_ID));

    if (potential == next_box_ID)
    {
        DP2(DEBUG3,"No more mailbox IDs for pid %d", getpid());
        return -ENOIDS;
    }


    DP2(DEBUG3,"mailbox ID is %d\n", potential);
    return potential;
}

/*!
    Input Invariant: you know that the number of used mailboxes is < number
                     of available mailboxes.
*/

int
find_empty_mailbox(void)
{
    int i = 0;
    for ( ; i < MAXMBOX; ++i)
        if (MailBoxTable[i].mbox_ID == 0)
            break;

    if (i == MAXMBOX)
        KERNEL_ERROR("Hey, no empty mailbox found!  Failed invariant.");

    DP2(DEBUG3, "found an unused mailbox at position %d", i);
    return i;
}

/*!
    Given a mailbox ID, returns 0 if a mailbox with that ID is found,
    or -EBADBOX if it is not.  If there is a valid mailbox, then position
    will be assigned the index value within MailBoxTable where the mailbox
    may be found.
*/

int
is_valid_mailbox(int ID, int *position)
{
    int i = 0;
    for ( ; i < MAXMBOX; ++i)
        if (MailBoxTable[i].mbox_ID == ID)
            break;

    if ((i < MAXMBOX) && position)
        *position = i;

    DP2(DEBUG3, "box ID %d is %s: position is %d\n",
        ID, ((i == MAXMBOX) ? "valid" : "invalid"), i);
    return (i != MAXMBOX) ? 0 : -EBADBOX;
}

/*!
    Given a mailbox 'box', checks to see if the mailbox still is
    allowed another slot.  If so, then checks to see if there are any
    slots free.  If yes, then starting from the beginning of the list of
    mail slots, finds the 1st slot that has a 0 mbox_ID (is unused), and
    returns a pointer to it.

    If the mailbox has no free slots available, then NULL is returned.

    If the system is out of slots to allocate, then the system will
    halt (as per revision 1.2 of the spec).
*/

mail_slot *
get_free_slot(mailbox *box)
{
    int i = 0;

    if (!box)
        KERNEL_ERROR("NULL mailbox");

    if (box->slots_count == box->max_slots_count)
    {
        DP2(DEBUG2, "No free slots for box ID %d", box->mbox_ID);
        return NULL;
    }

    if (slots_in_use == MAXSLOTS)
        KERNEL_ERROR("No free message slots available");

    for ( ; i < MAXSLOTS; ++i)
        if (message_slots[i].mbox_ID == 0)
            break;

    if (i == MAXSLOTS)
        KERNEL_ERROR("Accounting misfortune finding a free slot");

    ++slots_in_use;
    ++box->slots_count;
    DP2(DEBUG2, "Found a free slot: now using %d of %d\n",
        box->slots_count, box->max_slots_count);
    return &message_slots[i];
}

/*!
    Null if there are no messages, else the first message in the queue.
*/

mail_slot *
get_next_slot(mailbox *box)
{
    if (!box)
        KERNEL_ERROR("Null mailbox");

    return box->slots_front;
}

/*!
    Removes the first message in the message queue and advances to next message.

    For a 0 slot box, the routine simply returns without doing anything.
*/

void
free_slot(mailbox *box)
{
    mail_slot *s;

    if (!box)
        KERNEL_ERROR("Null mailbox");

    if (!box->slots_front)
    {
        /* This is the path a 0 slot mailbox will take */
        DP2(DEBUG, "Message queue already empty");
        return;
    }

    s = box->slots_front;
    box->slots_front = box->slots_front->next;

    if (box->slots_front == NULL)
        box->slots_back = NULL;

    --slots_in_use; 
    --box->slots_count;
    DP2(DEBUG2, "Box %d has freed a slot, now using %d of %d\n",
        box->mbox_ID, box->slots_count, box->max_slots_count);
    reinitialize_slot(s);
}

void
add_to_slot_list(mailbox *box, mail_slot *slot)
{
    if (!box)
        KERNEL_ERROR("Null mailbox");

    if (!slot)
        KERNEL_ERROR("Null mail slot");

    if (!box->slots_back) /* 1st to be enqueued */
    {
        box->slots_front = slot;
        box->slots_back = slot;
        slot->next = NULL;
    } else          /* Add to end of queue */
    {
        box->slots_back->next = slot;
        box->slots_back = slot;
        slot->next = NULL;
    }
}

/*!
    Does not zero message bytes: seems expensive, and shouldn't be necessary.
*/

void
reinitialize_slot(mail_slot *s)
{
    if (!s)
        KERNEL_ERROR("Mailslot is NULL");

    s->next = NULL;
    s->mbox_ID = 0;
    s->status = MBOX_CLEARED;
    s->pid = 0;
    s->bytes = 0;
}

/*!
    Begin to use the mailbox at index 'position' from the start of MailBoxTable.

    The fields filled out now are:
    (1) the mailbox ID
    (2) the number of possible slots for the mailbox
    (3) the maximum size of each of these slots
*/

void
initialize_mailbox(mailbox *box, int box_ID, int slots, int slot_size)
{
    box->mbox_ID = box_ID;
    box->max_message_size = slot_size;
    box->max_slots_count = slots;
    box->slots_count = 0;
    box->slots_front = NULL;
    box->slots_back = NULL;
    box->front = NULL;
    box->back = NULL;
    ++boxes_in_use;
    DP2(DEBUG2, "Box %d initialized to %d slots of max message size %d\n",
        box_ID, slots, slot_size);
}

/*!
    Used when releasing a mailbox.  Zeroes each slot and the mailbox itself.
*/

void
reinitialize_mailbox(mailbox *box)
{
    mail_slot *slot, *previous;

    if (!box)
        KERNEL_ERROR("Null mailbox");

    DP2(DEBUG, "Mailbox %d is being reinitialized: had %d of %d slots used \n",
        box->mbox_ID, box->slots_count, box->max_slots_count);

    box->mbox_ID = 0;
    box->max_message_size = 0;
    box->max_slots_count = 0;
    box->slots_count = 0;
    box->front = NULL;
    box->back = NULL;

    slot = box->slots_front;
    if (slot)
    {
        do {
            previous = slot;
            slot = slot->next;
            reinitialize_slot(previous);
        } while (slot);
    }
    --boxes_in_use;
}

/*!
    Add the process at index 'proc_entry' from start of process_table 
    to the queue waiting on mailbox 'box'.
*/

void
enqueue(mailbox *box, int process_index)
{
    proc_entry *p = process_table + process_index;

    p->pid = getpid();

    DP2(DEBUG, "Adding process %d to end of box %d queue\n",
        p->pid, box->mbox_ID);

    if (!box)
        KERNEL_ERROR("NULL mailbox");

    if (!box->back) /* 1st to be enqueued */
    {
        box->front = p;
        box->back = p;
        p->next = NULL;
    } else          /* Add to end of queue */
    {
        box->back->next = p;
        box->back = p;
        p->next = NULL;
    }

    DP2(DEBUG, "There are %d children\n", count_links(box));
}

/*!
    Removes the first element from the list of processes of mailbox 'box'.

    If the queue is empty, return NULL.
*/

proc_entry *
dequeue(mailbox *box, int type)
{
    proc_entry *p;

    if (!box)
        KERNEL_ERROR("Null mailbox");

    if (!box->front)
    {
        DP2(DEBUG, "Queue for box %d is empty\n", box->mbox_ID);
        return NULL;
    }

    p = box->front;

    DP2(DEBUG, "Dequeue of type %d requested by %d for mailbox %d\n",
               type, p->pid, box->mbox_ID);

    /* Dequeues a process from the mailbox's queue.  Processes that do
       sends should only unqueue processes that are receivers, and visa
       versa.  This switch statement checks for that.  Otherwise, if a bunch
       of senders are queued and one gets woken up, it will then up another
       sender when it finishes, even though this just woken process has
       immedately filled the empty slot.
    */
    switch (type)
    {
    case RECEIVER:
        /* Receivers can only be queued when all the slots are empty
           (no messages waiting to be received).  This this is called by the
           sender right after he adds his message, so there should be a single
           message in a slot waiting for an unblocked receiver.
        */
        DP2(DEBUG, "Dequeueing receiver: box %d has %d slots of %d max\n",
            box->mbox_ID, box->slots_count, box->max_slots_count);
        if (box->slots_count != 1)
            return NULL;
        break;
    case SENDER:
        /* Receiver process does this to attempt to unqueue a blocked sending process, if any.  However, to keep it from unblocking a receiver instead, I test to see what kind of task is on the mailbox's queue.

        */
        if (box->slots_count == 0)
            return NULL;
        break;
    case EITHER:
        /* used when we're releasing mailboxes. There never will be
           both senders and receivers (so "either") may not be the best name.
           It's more of a "just dequeue the damn things". */
        break;
    default:
        KERNEL_ERROR("Unknown dequeue request %d", type);
    }


    DP2(DEBUG,"Old front is %08x : new is %08x\n", box->front, box->front->next);
    box->front = box->front->next;

    /* If queue is now empty, then 'back' needs to point to NULL. */
    if (!box->front)
    {
        DP2(DEBUG, "Emptied process queue for %d\n", getpid());
        box->back = NULL;
    }

    DP2(DEBUG, "Removed process %d from front of box %d queue\n",
        p->pid, box->mbox_ID);

    p->next = NULL;
    return p;
}

/*!
    Returns with interrupts DISABLED.
*/

int
handle_enqueue_and_blocking(mailbox *box, const int BLOCK_TYPE)
{
    int status;

    if (!box)
        KERNEL_ERROR("Null mailbox");

    enqueue(box, CURRENT);
    /* interrupts are enabled in block_me() */
    status = block_me(BLOCK_TYPE);
    if (status != 0)
        KERNEL_ERROR("Error blocking process %d\n", getpid());
    disableInterrupts();                         

    /* See if zapped while blocked */
    if (is_zapped())
        status = -EZAPPED;         
    /* See if mailbox released while blocked */
    else if (is_valid_mailbox(box->mbox_ID, 0) == -EBADBOX)
        status = -EBOXRELEASED;

    return status;
}

/*!
    Called at startup to initialize interrupt vectors for the
    various "hardware" interrupts supported by the OS.

    Additionally, point all the syscall vectors to an error routine
    since they are not handled at this time.
*/

void
init_vectors(void)
{
    int i = 0;
    for ( ; i < MAXSYSCALLS; ++i)
        sys_vec[i] = nullsys;

    int_vec[CLOCK_DEV]  = clock_handler;
    int_vec[ALARM_DEV]  = bad_interrupt;
    int_vec[DISK_DEV]   = disk_handler;
    int_vec[TERM_DEV]   = term_handler;
    int_vec[MMU_INT]    = bad_interrupt;
    int_vec[SYS_INT]    = syscall_handler;
}

/*!
    Handles setting up all the 0 slot mailboxes use for
    synchronization of the devices.

    The IDs for the mailboxes are stored in an array that is
    associative by device number, so that they can be retrieve later
    when all we have is a device type.
*/

void
init_device_handlers(void)
{
    int status;

    /* mailbox for the clock device */
    status = MboxCreate(0, MAX_MESSAGE);
    if (status < 0)
        KERNEL_ERROR("Creating mailbox for the clock device");
    device_mbox_ID[CLOCK_DEV] = status;

    /* mailbox for the alarm device */
    status = MboxCreate(0, MAX_MESSAGE);
    if (status < 0)
        KERNEL_ERROR("Creating mailbox for the alarm device");
    device_mbox_ID[ALARM_DEV] = status;

    /* mailbox for the disk devices */
    status = MboxCreate(0, MAX_MESSAGE);
    if (status < 0)
        KERNEL_ERROR("Creating mailbox for the disk devices");
    device_mbox_ID[DISK_DEV] = status;

    /* mailbox for the terminal devices */
    status = MboxCreate(0, MAX_MESSAGE);
    if (status < 0)
        KERNEL_ERROR("Creating mailbox for the terminal devices");
    device_mbox_ID[TERM_DEV] = status;

    /* mailbox for the Memory Management Unit (MMU) device */
    status = MboxCreate(0, MAX_MESSAGE);
    if (status < 0)
        KERNEL_ERROR("Creating mailbox for the MMU");
    device_mbox_ID[MMU_INT] = status;

    /* mailbox for syscalls */
    status = MboxCreate(0, MAX_MESSAGE);
    if (status < 0)
        KERNEL_ERROR("Creating mailbox for syscall use");
    device_mbox_ID[SYS_INT] = status;

    DP2(DEBUG3, "Finished initializing device handler mailboxes.\n");
}

void
reinitialize_proc_entry(proc_entry *p)
{
    p->pid = 0;
    p->next = NULL;
    p->msg_ptr = NULL;
    p->msg_size = 0; 
    p->type = PROCESS_INVALID;
}

/*!
    Returns 1 if there is a device is blocked on its device mailbox, else 0.
*/

int
check_io(void)
{
    int status = 0;
    status += MailBoxTable[device_mbox_ID[CLOCK_DEV]].front != NULL;    
    status += MailBoxTable[device_mbox_ID[ALARM_DEV]].front != NULL;    
    status += MailBoxTable[device_mbox_ID[DISK_DEV]].front != NULL;    
    status += MailBoxTable[device_mbox_ID[TERM_DEV]].front != NULL;    
    status += MailBoxTable[device_mbox_ID[MMU_INT]].front != NULL;    
    status += MailBoxTable[device_mbox_ID[SYS_INT]].front != NULL;    
    return status ? 1 : 0;
}

