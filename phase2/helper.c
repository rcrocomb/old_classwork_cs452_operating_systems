#include "helper.h"
#include "utility.h"
#include "handler.h"

#include <phase2.h>
#include <phase1.h>
#include <string.h>

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
extern void (*sys_vec[])(sysargs *args);

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
    int potential = next_box_ID;

    /* do not want a mbox with this certain ID: use it to identify
       empty table slots */
    potential += potential == EMPTY_BOX_ID;

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
    } while ((i < MAXMBOX) && ( (potential + 1) != next_box_ID));

    if ( (potential + 1) == next_box_ID)
    {
        DP2(DEBUG3,"No more mailbox IDs for pid %d", getpid());
        return -ENOIDS;
    }


    DP2(DEBUG4,"mailbox ID is %d\n", potential);
    ++next_box_ID;
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
        if (MailBoxTable[i].mbox_ID == EMPTY_BOX_ID)
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
    int status;

    for ( ; i < MAXMBOX; ++i)
        if (MailBoxTable[i].mbox_ID == ID)
            break;

    if ((i < MAXMBOX) && position)
        *position = i;

    if (i == MAXMBOX)
        status = -EBADBOX;
    else
        status = 0;

    DP2(DEBUG, "box ID %d is %s: position is %d -- returning %d\n",
        ID, ((status == 0) ? "valid" : "invalid"), i, status);

    return status;
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
        DP2(DEBUG, "No free slots for box ID %d\n", box->mbox_ID);
        return NULL;
    }

    if (slots_in_use == MAXSLOTS)
        KERNEL_ERROR("No free message slots available");

    for ( ; i < MAXSLOTS; ++i)
        if (message_slots[i].mbox_ID == EMPTY_BOX_ID)
            break;

    if (i == MAXSLOTS)
        KERNEL_ERROR("Accounting misfortune finding a free slot");

    DP2(DEBUG, "Found a free slot\n");
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
release_slot(mailbox *box)
{
    mail_slot *s;

    if (!box)
        KERNEL_ERROR("Null mailbox");

    if (!box->slots_front)
    {
        /* This is the path a 0 slot mailbox will take */
        DP2(DEBUG2, "Message queue already empty");
        return;
    }

    s = box->slots_front;
    box->slots_front = box->slots_front->next;

    if (box->slots_front == NULL)
        box->slots_back = NULL;

    --slots_in_use; 
    --box->slots_count;
    initialize_slot(s);

    DP2(DEBUG2, "Box %d has freed a slot, now using %d of %d\n",
        box->mbox_ID, box->slots_count, box->max_slots_count);

}

/*!
    Adds slot 'slot' to the end of the queue of message slots for mailbox 'box'.
*/

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

    ++slots_in_use;
    ++box->slots_count;

}

/*!
    Folds in all the stuff needed when copying data for slotful mailboxes.
*/

void
handle_message_copy(mailbox *box, mail_slot *slot, void *msg_ptr, int msg_size)
{
    if (!box)
        KERNEL_ERROR("Mailbox is NULL");

    if (!slot)
        KERNEL_ERROR("Mailslot is NULL");

    DP2(DEBUG, "Sender to slot with no waiting receivers\n");

    /* Copy message to the free slot. */
    slot->mbox_ID = box->mbox_ID;
    slot->pid = getpid();
    memcpy(slot->data, msg_ptr, msg_size);
    slot->bytes = msg_size;
    add_to_slot_list(box, slot);

    DP2(DEBUG3, "In message slot: Message is %d bytes, '%s'\n",
                slot->bytes,slot->data);
    DP2(DEBUG, "Now using %d slots of %d allowed\n",
               box->slots_count, box->max_slots_count);
}

/*
    A slot freed up by an MboxReceive might be immediately used up
    again if any senders were blocked.
*/

void
handle_pending_senders(mailbox *box)
{
    mail_slot *s;

    if (!box)
        KERNEL_ERROR("Null mailbox");

    if (box->front)
    {
        DP2(DEBUG, "Reusing slot for queued message\n");
    
        /* Darned well better succeed since we just emptied a slot and
           interrupts are disabled */
        s = get_free_slot(box);
        if (!s)
            KERNEL_ERROR("Shifting pid %d from queue to empty slot",
                         box->front->pid);

        /* Must handle copy now, before process we're depending upon
           does anything wacky.  Use handles to message within process_entry of
           process on queue to move message data to the newly acquired slot. Add
           that slot to list associated with this mailbox. */
        handle_message_copy(box, s, box->front->msg_ptr, box->front->msg_size);
    }
}

/*!
    Does not zero message bytes: seems expensive, and shouldn't be necessary.
*/

void
initialize_slot(mail_slot *slot)
{
    if (!slot)
        KERNEL_ERROR("Mailslot is NULL");

    DP2(DEBUG3, "Init slot %d\n", slot - message_slots);

    slot->next = NULL;
    slot->mbox_ID = EMPTY_BOX_ID;
    slot->pid = 0;
    slot->bytes = 0;
}

/*!
    Begin to use the mailbox at index 'position' from the start of MailBoxTable.

    The fields filled out now are:
    (1) the mailbox ID
    (2) the number of possible slots for the mailbox
    (3) the maximum size of each of these slots
*/

void
use_mailbox(mailbox *box, int box_ID, int slots, int slot_size)
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
    Used at startup (ONLY!) to set all mailboxes to a consistent state.

    Really, only the setting of mbox_ID is necessary, and that's only
    necessary because Patrick considers a box ID of 0 to be valid, whereas
    I did not.  This was making all my mailbox IDs be off by one (I'd have
    8 where he'd have 7), and I don't want to have to deal with any
    comparison problems that might cause.

    To sum up: poop.
*/

void
initialize_mailbox(mailbox *box)
{
    if (!box)
        KERNEL_ERROR("Null mailbox");

    box->mbox_ID = EMPTY_BOX_ID;
    box->max_message_size = 0;
    box->max_slots_count = 0;
    box->slots_count = 0;
    box->front = NULL;
    box->back = NULL;
    box->slots_front = NULL;
    box->slots_back = NULL;
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

    box->mbox_ID = EMPTY_BOX_ID;
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
            initialize_slot(previous);
        } while (slot);
    }
    --boxes_in_use;
}

/*!
    Add the process at index 'proc_entry' from start of process_table 
    to the queue waiting on mailbox 'box'.
*/

void
enqueue(mailbox *box, proc_entry *p)
{
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
dequeue(mailbox *box, const enum process_type type)
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
    case PROCESS_RECEIVER:
        /* Receivers can only be queued when all the slots are empty
           (no messages waiting to be received).  This this is called by the
           sender right after he adds his message, so there should be a single
           message in a slot waiting for an unblocked receiver.
        */
        DP2(DEBUG, "Dequeueing receiver: box %d has %d slots of %d max\n",
            box->mbox_ID, box->slots_count, box->max_slots_count);
        if ((box->slots_count != 0) && (box->max_slots_count != 0))
            return NULL;
        break;
    case PROCESS_SENDER:
        /* Receiver process does this to attempt to unqueue a blocked sending process, if any.  However, to keep it from unblocking a receiver instead, I test to see what kind of task is on the mailbox's queue.

        */
        if ((box->slots_count == 0) && (box->max_slots_count != 0))
            return NULL;
        break;
    case PROCESS_EITHER:
        /* used when we're releasing mailboxes. There never will be
           both senders and receivers (so "either") may not be the best name.
           It's more of a "just dequeue the damn things". */
        break;
    default:    /* for PROCESS_INVALID, too. */
        KERNEL_ERROR("Unknown dequeue request %d", type);
    }


    DP2(DEBUG4,"Old front is %08x : new is %08x\n", box->front, box->front->next);
    box->front = box->front->next;

    /* If queue is now empty, then 'back' needs to point to NULL. */
    if (!box->front)
    {
        DP2(DEBUG, "Emptied process queue for pid %d\n", getpid());
        box->back = NULL;
    }

    DP2(DEBUG, "Removed process %d from front of box %d queue\n",
        p->pid, box->mbox_ID);

    p->next = NULL;
    return p;
}

/*!
    Returns with interrupts DISABLED.

    Note that box ID has to be saved before we block, in case box is
    released while we are blocked: we won't then be able to check and see
    if the box ID we had was valid.

    Returns 0 if everything okay
            -EZAPPED if the process was zapped while blocked
            -EBOXRELEASED if the mailbox it is attached to is released
*/

int
handle_enqueue_and_blocking
(
    mailbox *box,
    void *msg_ptr,
    int msg_size,
    const enum process_type type
)
{
    #define MAGICAL_OFFSET 50
    int status;
    int box_ID = box->mbox_ID;

    if (!box)
        KERNEL_ERROR("Null mailbox");

    set_process_entry_info(msg_ptr, msg_size, type);

    DP2(DEBUG2, "Blocking process %d on box %d\n", getpid(), box->mbox_ID);

    enqueue(box, &process_table[CURRENT]);
    /* interrupts are enabled in block_me(): returns 0 on success */
    status = block_me( (int)type + MAGICAL_OFFSET);
    if (status != 0)
        KERNEL_ERROR("Error blocking process %d\n", getpid());
    disableInterrupts();                         

    /* See if zapped while blocked */
    if (is_zapped())
        status = -EZAPPED;         
    /* See if mailbox released while blocked */
    else if (is_valid_mailbox(box_ID, 0) == -EBADBOX)
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
init_device_mailboxes(void)
{
    int status;
    int i = 0;

    /* mailbox for the clock device */
    status = MboxCreate(0, MAX_MESSAGE);
    if (status < 0)
        KERNEL_ERROR("Creating mailbox for the clock device");
    device_mbox_ID[CLOCK_DEV] = status;

    /* mailbox for the disk devices */
    for (i = 0 ; i < DISK_UNITS; ++i)
    {
        status = MboxCreate(0, MAX_MESSAGE);
        if (status < 0)
            KERNEL_ERROR("Creating mailbox for disk %d device", i);
        device_mbox_ID[DISK_DEV + i] = status;
    }

    /* mailbox for the term 1 device */
    for (i = 0; i < TERM_UNITS; ++i)
    {
        status = MboxCreate(0, MAX_MESSAGE);
        if (status < 0)
            KERNEL_ERROR("Creating mailbox for  term %d device", i);
        device_mbox_ID[TERM_DEV + i] = status;
    }

    DP2(DEBUG3, "Finished initializing device handler mailboxes.\n");
}

void
initialize_proc_entry(proc_entry *p)
{
    p->pid = 0;
    p->next = NULL;
    p->msg_ptr = NULL;
    p->msg_size = 0; 
    p->type = PROCESS_INVALID;
}

void
set_process_entry_info(void *msg_ptr, int msg_size, const enum process_type type)
{
    process_table[CURRENT].msg_ptr = msg_ptr;
    process_table[CURRENT].msg_size = msg_size;
    process_table[CURRENT].type = type;
}

/*!
    Set all mailboxes to a consistent state at OS startup.
*/

void
initialize_mailbox_table(void)
{
    int i = 0;
    for ( ; i < MAXMBOX; ++i)
        initialize_mailbox(MailBoxTable + i);
}

void
initialize_slot_table(void)
{
    int i = 0;
    for ( ; i < MAXSLOTS; ++i)
        initialize_slot(message_slots + i);
}

/*!
    Returns 1 if there is a device is blocked on its device mailbox, else 0.

    Note that all the fields of the non-initialized devices
    (ALARM_DEV), etc. are set to 0 (NULL) because the array is global
    (well, of static duration), so these comparisons should be safe.
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

/*!
    Handles messages directed at mail boxes for which there are no slots.
*/

int
slotless_sender(mailbox *box, void *msg_ptr, int msg_size)
{
    int status = 0, message_size;

    DP2(DEBUG, "0 slot mailbox\n");

    if (!box->front || (box->front && box->front->type == PROCESS_SENDER))
    {
        /* Nothing is waiting, so we must block. Or, what is
           queued is another sender. Must join the tail of the queue */
        status = handle_enqueue_and_blocking(box, msg_ptr, msg_size,
                                             PROCESS_SENDER);
        if (status == 0)
            CLEAR_PROC_INFO;
        /* else zapped or mailbox released while blocked */
    } else if (box->front->type == PROCESS_RECEIVER)
    {
        /* What's queued is a receiver: copy */
        DP2(DEBUG, "Sending to queued receiver\n");
        message_size = MIN(msg_size, box->front->msg_size);
        DP2(DEBUG, "Sending %d bytes, first 4 are %08x\n",
            message_size, *(int *)(msg_ptr));
        memcpy(box->front->msg_ptr, msg_ptr, message_size);
        box->front->msg_size = message_size;

        /* Unblock 1st process that was waiting to receive a message */
        release_process(box, PROCESS_RECEIVER);

    } else
        KERNEL_ERROR("Bad or unknown process type for pid %d '%d'",
                     getpid(), box->front->type);

    DP2(DEBUG, "Exiting with status %d\n", status);
    return status;
}

/*!
    Sends messages in the case that the mailbox to which messages are
    directed has slots (free or occupied: as long as they exist 
*/

int
slotful_sender(mailbox *box, mail_slot *slot, void *msg_ptr, int msg_size)
{
    int status = 0, message_size;

    DP2(DEBUG, "Slotful sender\n");

    /* There are slots for this mailbox, but they may all be full */
    if (!slot)
    {
        /* All slots full */
        DP2(DEBUG, "No slot for %d in %d, so blocking and looping\n",
            getpid(), box->mbox_ID);

        status = handle_enqueue_and_blocking(box, msg_ptr, msg_size,
                                             PROCESS_SENDER);
/*
        if (status == 0)
            CLEAR_PROC_INFO;
*/
        /* non-zero status: zapped or mailbox released while blocked */
    } else
    {
        /* Non-blocking side */

        /* Got a free slot in a slotful mailbox: are there receivers pending? */
        if (box->front)
        {
            DP2(DEBUG, "Sender to queue with receiver blocked: copying "    
                       "to receiver directly\n");

            /* Didn't need slot after all: receivers are pending and I      
               must copy data directly to their msg_ptrs. Thankfully,
               we've not done anything with the slot, so we'll pretend
               we just never heard of it. */

            message_size = MIN(msg_size, box->front->msg_size);
            memcpy(box->front->msg_ptr, msg_ptr, message_size);
            box->front->msg_size = message_size;
            DP2(DEBUG, "Copied %d bytes to receiver: first few are %08x\n", 
                       box->front->msg_size, *(int *)box->front->msg_ptr);
            release_process(box, PROCESS_RECEIVER);
        } else
            /* no queued up receivers: put data in slot */
            handle_message_copy(box, slot, msg_ptr, msg_size);
    }

    DP2(DEBUG, "Exiting with status %d\n", status);
    return status;
}

/*!
    Status value is count of bytes received, or error code if there
    was a problem.
*/

int
slotless_receive(mailbox *box,  void *msg_ptr, int msg_size)
{
    int status = 0, message_size;

    if (!box->front || (box->front && box->front->type == PROCESS_RECEIVER))
    {
        /* Nothing enqueued, or what is enqueued is another receiver.
           Must block in both cases. */
        DP2(DEBUG, "Blocking on 0 slot receive\n for box %d", box->mbox_ID);

        status = handle_enqueue_and_blocking(box, msg_ptr, msg_size,
                                             PROCESS_RECEIVER);
        if (status == 0)
        {
            DP2(DEBUG, "Receiver unblocked for 0 slot.  Message is %d bytes "
                       ":%08x : %c\n", msg_size, *(int *)msg_ptr,
                       TERM_STAT_CHAR( *((int *)msg_ptr)));

            status = process_table[CURRENT].msg_size;
            CLEAR_PROC_INFO;
        }
        /* else zapped or mailbox released while blocked */

    } else if (box->front->type == PROCESS_SENDER)
    {
        /* Something is waiting: we needn't block */
        if (!box->front->msg_ptr)
            KERNEL_ERROR("Source pointer is NULL for 0 slot mailbox");

            /* 0 slot process: move from sender's msg_ptr directly to ours. */
            /* It's okay to do a 0 byte memcpy to a NULL ptr, so I needn't
               stress on that. */
            message_size = MIN(msg_size, box->front->msg_size);
            memcpy(msg_ptr, box->front->msg_ptr, message_size);
            status = message_size;
            box->front->msg_size = message_size;
            release_process(box, PROCESS_SENDER);
    } else
        KERNEL_ERROR("Bad or unknown process type for pid %d '%d'",
                     getpid(), box->front->type);

    return status;
}

/*!

*/

int
slotful_receive(mailbox *box, mail_slot *slot, void *msg_ptr, int msg_size)
{
    int status = 0;

    /* mailbox has slots, but they may all be empty. */
    if (!slot)
    {
        /* All empty.  Must block until we get a sender */
        status = handle_enqueue_and_blocking(box, msg_ptr, msg_size,
                                             PROCESS_RECEIVER);
        if (status == 0)
        {
            status = process_table[CURRENT].msg_size;
            CLEAR_PROC_INFO;
        }
        /* else zapped or mailbox released while blocked */
    } else
    {
        /* Non-blocking side */

        /* Got a message slot in a slotful mailbox */

        /* This is how much data we will copy out of the slot */
        if (slot->bytes <= msg_size)
        {
            memcpy(msg_ptr, slot->data, slot->bytes);
            DP2(DEBUG, "After copy, %d bytes '%s'\n",
                slot->bytes, (char *)msg_ptr);
            status = slot->bytes;
            release_slot(box);
            handle_pending_senders(box);
            release_process(box, PROCESS_SENDER);
        }
        else
            status = -ESLOTSIZE;
    }

    return status;
}

void
release_process(mailbox *box, const enum process_type type)
{
    int ret;
    proc_entry *p;

    if (!box)
        KERNEL_ERROR("Null mailbox");

    /* Unblock 1st process that was waiting to send/receive a message */
    p = dequeue(box, type);
    if (p)
    {
        ret = unblock_proc(p->pid);
        if (ret != 0)
            KERNEL_ERROR("Failed to unblock %d (supposedly a blocked '%s'): "
                         "return code was %d\n",
                         p->pid,
                        ((type == PROCESS_SENDER) ? "sender" : "receiver"),
                        ret);
    }/* else
        KERNEL_ERROR("trying to dequeue an empty queue");
   */ 
}
