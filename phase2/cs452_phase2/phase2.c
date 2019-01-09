/* ------------------------------------------------------------------------
   phase2.c

   University of Arizona
   Computer Science 452
   Spring 2005
   Robert Crocombe

   ------------------------------------------------------------------------ */

#include <phase1.h>
#include <phase2.h>
#include <usloss.h>

#include "message.h"
#include "utility.h"
#include "helper.h"

#include <string.h>

/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
int start2 (char *);

/* In Patrick's library, but not in a header.  Super. */
/* extern void check_kernel_mode(char *); */


/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 1;

proc_entry process_table[MAXPROC];

/* the mail boxes */
int boxes_in_use;
mail_box MailBoxTable[MAXMBOX];

int next_box_ID;

int slots_in_use;
mail_slot message_slots[MAXSLOTS];

/* Stores the IDs of the mailboxes associated with each device handler. */
int device_mbox_ID[SYSCALL + 1];


/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name - start1
   Purpose - Initializes mailboxes and interrupt vector.
             Start the phase2 test process.
   Parameters - one, default arg passed by fork1, not used here.
   Returns - one to indicate normal quit.
   Side Effects - lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int
start1(char *arg)
{
    int kid_pid, status;

    DP2(DEBUG3,"beginning\n");
    
    KERNEL_MODE_CHECK;
    disableInterrupts();

    /* fine with mailbox table being zeroed */
    /* fine with slot table being zeroed */
    /* fine with process table being zeroed */
    init_vectors();
    init_device_handlers();

    enableInterrupts();

   /* Create a process for start2, then block on a join until start2 quits */
    DP2(DEBUG3,"forking start2 process\n");

    kid_pid = fork1("start2", start2, NULL, 4 * USLOSS_MIN_STACK, 1);
    if (join(&status) != kid_pid)
      KERNEL_WARNING("join status != start2's pid: status == %d vs pid == %d\n",
                     status, kid_pid);

   return 0;
}


/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose - gets a free mailbox from the table of mailboxes and initializes it 
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
             mailbox id.
   Side Effects - initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int
MboxCreate(int num_slots, int slot_size)
{
    int table_position;
    mailbox *box;

    KERNEL_MODE_CHECK;
    disableInterrupts();

    next_box_ID = get_next_ID();
    if (next_box_ID == -ENOIDS)
        KERNEL_ERROR("All out of mailbox IDs.  Bummer.");

    if (boxes_in_use == MAXMBOX)
    {
        DP2(DEBUG2, "All mailboxes full: %d\n", boxes_in_use);
        return -ENOBOX;
    }

    if ((slot_size < 0) || (slot_size > MAX_MESSAGE))
    {
        DP2(DEBUG, "Invalid slot_size %d\n", slot_size);
        return -ESLOTSIZE;
    }
    
    /* Find empty slot for this mailbox */
    table_position = find_empty_mailbox();
    box = MailBoxTable + table_position;
    
    /* Fill in mail box info*/
    initialize_mailbox(box, next_box_ID, num_slots, slot_size);
    enableInterrupts();

    return next_box_ID;
}


/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int
MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
    int invalid_ID, position, message_size, ret, status = 0;
    mailbox *box;
    mail_slot *slot;
    proc_entry *p;

    KERNEL_MODE_CHECK;
    disableInterrupts();

    invalid_ID = is_valid_mailbox(mbox_id, &position);
    if (invalid_ID)
    {
        DP2(DEBUG, "Invalid mbox_ID %d w/ ptr %08x of size %d\n",
                   mbox_id, msg_ptr, msg_size);
        status = invalid_ID;
        goto out;
    }

    box = MailBoxTable + position;

    if (!msg_ptr)
    {
        DP2(DEBUG, "Null message pointer to mailbox %d\n", mbox_id);
        status = -ENULLMSG;
        goto out;
    }

    if ((msg_size > box->max_message_size) || (msg_size < 0))
    {
        DP2(DEBUG, "Invalid message size of %d to box %d\n", msg_size, mbox_id);
        status = -EMSGSIZE;
        goto out;
    }

    slot = get_free_slot(box); 

    DP2(DEBUG3, "Free slot is %08x\n", slot);

    if (slot && box->max_slots_count == 0)
        KERNEL_ERROR("Had a slot for a 0 slot mailbox");

    /* 0 slot mailbox special case */
    if (box->max_slots_count == 0)
    {
        DP2(DEBUG, "0 slot mailbox\n");

        /* Okay, so there's something blocked on the 0 slot.  Is it
            another sender, or is it a receiver? */

        if (box->front->type == PROCESS_RECEIVER)
        {
            /* What's queued is a receiver: copy */
            DP2(DEBUG, "Sending to blocked task for 0 slot mailbox\n");
            message_size = MIN(msg_size, box->front->msg_size);
            memcpy(msg_ptr, box->front->msg_ptr, message_size);
            box->front->msg_ptr = NULL;
            box->front->msg_size = 0;
            box->front->type = PROCESS_INVALID;

        } else if (box->front->type == PROCESS_SENDER)
        {
            /* It was another sender. must enqueue and block 0-slot sender */
            DP2(DEBUG, "Blocking 0 slot sender\n");

            /* Get a hold of message stuff needed by opposite polarity
               task to get data before it unblocks us. */
            process_table[CURRENT].msg_ptr = msg_ptr;
            process_table[CURRENT].msg_size = msg_size;
            process_table[CURRENT].type = PROCESS_SENDER;

            ret = handle_enqueue_and_blocking(box, MBOX_BLOCKED_SEND);
            if (ret != 0)
                /* Zapped or mailbox released while blocked */
                goto out;

        } else
            KERNEL_ERROR("Bad or unknown process type for pid %d '%d'",
                         getpid(), box-<front->type);
    } else 
    {

        /* There are slots for this mailbox, but they may all be full */
        while (!slot)
        {
            DP2(DEBUG2, "No slot for %d in %d, so blocking and looping\n",
                getpid(), box->mbox_ID);

            ret = handle_enqueue_and_blocking(box, MBOX_BLOCKED_SEND);
            if (ret != 0)
                /* Zapped or mailbox released while blocked */
                goto out;
    
            /* Try for slot again */
            slot = get_free_slot(box); 
        }
    }
/* Interrupts are disabled here */

    /* Got a free slot in a slotful mailbox */
    if (slot)
    {
        /* Copy message to the free slot. */
        slot->mbox_ID = mbox_id;
        slot->status = MBOX_CLEARED;
        slot->pid = getpid();
        memcpy(slot->data, msg_ptr, msg_size);
        slot->bytes = msg_size;
        add_to_slot_list(box, slot);

        DP2(DEBUG3, "In message slot: Message is %d bytes, '%s'\n",
                    slot->bytes,slot->data);
        DP2(DEBUG2, "Now using %d slots of %d allowed\n",
                    box->slots_count, box->max_slots_count);
    }

    /* Unblock 1st process that was waiting for a message to arrive:
       harmless when the queue is empty */
    p = dequeue(box, RECEIVER);
    if (p)
    {
        DP2(DEBUG, "Unblocking process %d\n", p->pid);
        ret =  unblock_proc(p->pid);   /* double enable interrupts because of this  */
        if (ret != 0)
            KERNEL_ERROR("Failed to unblock process %d", p->pid);
    }

out:
    enableInterrupts();
    return 0;
}


/* ------------------------------------------------------------------------
   Name - MboxReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
             Block the receiving process if no msg available.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args.
   Side Effects - none.

    As far as I can tell for 0 slot mailboxes, it is okay for two
    different processes to send to the same mailbox without an intervening
    receive: both will block and the 1st will be unblocked after the first
    receive, the 2nd after a second receive.

   ----------------------------------------------------------------------- */
int
MboxReceive(int mbox_id, void *msg_ptr, int msg_size)
{
    int invalid_ID, position, message_size, ret, status = 0;
    mailbox *box;
    mail_slot *slot;
    proc_entry *p;

    DP2(DEBUG2, "Receiving from %d into %08x with max of %d\n",
        mbox_id, msg_ptr, msg_size);

    KERNEL_MODE_CHECK;
    disableInterrupts();

    invalid_ID = is_valid_mailbox(mbox_id, &position);
    if (invalid_ID)
    {
        DP2(DEBUG, "Invalid mbox_ID %d w/ ptr %08x of size %d\n",
                   mbox_id, msg_ptr, msg_size);
        status = invalid_ID;        
        goto out;
    }

    box = MailBoxTable + position;

    // 0 slot mailboxes can have null message pointers.  Who knew?
    if ((box->max_slots_count != 0) && !msg_ptr)
    {
        DP2(DEBUG, "Null message pointer getting from mailbox %d\n", mbox_id);
        status = -ENULLMSG;
        goto out;
    }

    // Sun Feb 20 14:35:47 MST 2005
    //
    // test case 03 makes it appear that sending in msg_size >
    //max_message_size supported by the box is okay

    //if ((msg_size > box->max_message_size) || (msg_size < 0))
    if ((msg_size > MAX_MESSAGE) || (msg_size < 0))
    {
        DP2(DEBUG, "Invalid max message size of %d getting from box %d\n",
            msg_size, mbox_id);
        status = -EMSGSIZE;
        goto out;
    }

    /* Get message on front of queue for this mailbox: block if Null */
    slot = get_next_slot(box);

    DP2(DEBUG3, "Message slot is %08x\n", slot);

    if (slot && box->max_slots_count == 0)
        KERNEL_ERROR("Had a slot for a 0 slot mailbox");

    if (box->max_slots_count == 0)
    {
        /* 0 slot mailboxes */
        if (box->front->type == PROCESS_SENDER)
        {
            /* Something is waiting: we needn't block */
            if (!box->front->msg_ptr)
                KERNEL_ERROR("Source pointer is NULL for 0 slot mailbox");

            /* 0 slot process: move from sender's msg_ptr directly to ours. */
            /* It's okay to do a 0 byte memcpy to a NULL ptr, so I needn't
               stress on that. */
            message_size = MIN(msg_size, box->front->msg_size);
            memcpy(msg_ptr, box->front->msg_ptr, message_size);
            /* Clear the sneaky 0 slot info from the sender */
            box->front->msg_ptr = NULL;
            box->front->msg_size = 0;
            box->front->type = PROCESS_INVALID;
            status = message_size;

        } else if (box->front->type == PROCESS_RECEIVER)
        {
            /* Must block 0 slot receiver */

            /* Get a hold of message stuff needed by opposite polarity
               task to get data before it unblocks us. */
            process_table[CURRENT].msg_ptr = msg_ptr;
            process_table[CURRENT].msg_size = msg_size;
            process_table[CURRENT].type = PROCESS_RECEIVER;

            ret = handle_enqueue_and_blocking(box, MBOX_BLOCKED_RECEIVE);
            if (ret != 0)
                /* Zapped or mailbox released while blocked */
                goto out;
        } else
            KERNEL_ERROR("Bad or unknown process type for pid %d '%d'",
                         getpid(), box-<front->type);
    } else 
    {
        /* mailbox has slots, but they may all be full. */
        while (!slot)
        {
            ret = handle_enqueue_and_blocking(box, MBOX_BLOCKED_RECEIVE);
            if (ret != 0)
                /* Zapped or mailbox released while blocked */
                goto out;

            /* Try for free slot again */
            slot = get_next_slot(box);
        }
    }

    /* Got a free slot in a slotful mailbox*/
    if (slot)
    {
        /* This is how much data we will copy out of the slot */
        if (slot->bytes <= msg_size)
        {
            memcpy(msg_ptr, slot->data, slot->bytes);
            DP2(DEBUG2, "After copy, %d bytes '%s'\n",
                slot->bytes, (char *)msg_ptr);
            status = slot->bytes;
            free_slot(box);
        }
        else
        {
            status = -ESLOTSIZE;
        }

    }

    /* Unblock 1st process that was waiting to send a message (needs a
       free slot) */
    p = dequeue(box, SENDER);
    if (p)
    {
        ret = unblock_proc(p->pid);
        if (ret != 0)
            KERNEL_ERROR("Failed to unblock %d: return code was %d\n",
                         p->pid, ret);
    }

out:
    enableInterrupts();
    return status;
}

/*!
    Releases a previously created mailbox.  All processes waiting on
    the mailbox return a status of -3 (i.e., notice that their mailbox
    is gone and they should go away).
*/

int
MboxRelease(int box_ID)
{
    proc_entry *queued, *previous;
    mailbox *box;
    int position, invalid, ret;

    DP2(DEBUG, "Running\n");

    KERNEL_MODE_CHECK;
    disableInterrupts();

    invalid = is_valid_mailbox(box_ID, &position);
    if (invalid)
        return invalid;

    box = MailBoxTable + position;
    queued = box->front;

    /* Clear out mailbox info */
    reinitialize_mailbox(box);

    /* Unblock all processes that were blocked so that they can see
       mailbox was released and go away.  */
    while (queued)
    {
        DP2(DEBUG, "Front is %08x\n", queued);

        /* enables interrupts */
        ret = unblock_proc(queued->pid);
        if (ret != 0)
            KERNEL_ERROR("Unable to unblock process %d", queued->pid);

        DP2(DEBUG,"Unblocked process %d\n", queued->pid);
        
        disableInterrupts();
        previous = queued;
        queued = queued->next;
        reinitialize_proc_entry(previous);
    }


    if (is_zapped())
        return -EZAPPED;

    enableInterrupts();

    DP2(DEBUG, "MboxRelease is finished\n");
    return 0;
}

/*!
    Uhm, process doesn't block, so the -3 return code for "process was
    zapped while blocked on the mailbox" is darned unlikely to occur.
*/

int
MboxCondSend(int box_ID, void *msg_ptr, int msg_size)
{
    mailbox *box;
    int position;
    int invalid;
    int status;

    KERNEL_MODE_CHECK;
    disableInterrupts();

    invalid = is_valid_mailbox(box_ID, &position);
    if (invalid)
        return invalid;

    box = MailBoxTable + position;

    /* 0 slot case */
    if (box->max_slots_count == 0)
    {
        if (!box->front)
            status = -EWOULDBLOCK;
        else
            status = MboxSend(box_ID, msg_ptr, msg_size);
    } else
    {
        /* slotful mailbox maxed out slots */
        if (box->slots_count == box->max_slots_count)
            status = -EWOULDBLOCK;
        /* slotful mailbox no slots left in world */ 
        else if (slots_in_use == MAXSLOTS)
            status = -ENOSLOTS;
        else
            status = MboxSend(box_ID, msg_ptr, msg_size);
    }

    enableInterrupts();
    return status;
}

/*!
    Process doesn't block, so -3 return code for "process was zapped()
    while blocked on the mailbox" is darned unlikely to occur.
*/

int
MboxCondReceive(int box_ID, void *msg_ptr, int msg_max_size)
{
    mailbox *box;
    int position;
    int invalid;
    int status;

    KERNEL_MODE_CHECK;
    disableInterrupts();

    invalid = is_valid_mailbox(box_ID, &position);
    if (invalid)
        return invalid;

    box = MailBoxTable + position;

    /* 0 slot box */
    if (box->max_slots_count == 0)
    {
        if (!box->front)
            status = -EWOULDBLOCK;
        else
            status = MboxSend(box_ID, msg_ptr, msg_max_size);
    } else
    {
        /* slotful boxes */

        if (box->slots_count == box->max_slots_count)
            status = -EWOULDBLOCK;
        else if (slots_in_use == MAXSLOTS)
            status = -ENOSLOTS;
        else
            status = MboxReceive(box_ID, msg_ptr, msg_max_size);
    }

    enableInterrupts();
    return status;
}

/*!
        
*/

int
waitdevice(int type, int unit, int *device_status)
{
    int box_ID, status, status_reg;

    KERNEL_MODE_CHECK;
    disableInterrupts();

    if ((type < CLOCK_DEV) || (type > SYSCALL))
        KERNEL_ERROR("Invalid device type %d", type);
            
    box_ID = device_mbox_ID[box_ID]; 
    status = MboxReceive(box_ID, &status_reg, sizeof(status_reg));
    if (status != sizeof(status_reg))
        KERNEL_ERROR("Error in receive for device %d at box %d", type, box_ID);

    *device_status = status_reg;
    enableInterrupts();

    if (is_zapped())
        return -EWAITZAPPED;
    return 0;
}

