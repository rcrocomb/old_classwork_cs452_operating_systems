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


int debugflag2 = 0;

proc_entry process_table[MAXPROC];

/* the mail boxes */
int boxes_in_use;
mail_box MailBoxTable[MAXMBOX];

int next_box_ID;

int slots_in_use;
mail_slot message_slots[MAXSLOTS];

/* Stores the IDs of the mailboxes associated with each device handler. */
int device_mbox_ID[MAX_UNITS * 4];

sys_vec_func_t sys_vec[MAXSYSCALLS];

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

    initialize_mailbox_table();
    initialize_slot_table();
    /* fine with process table being zeroed */
    init_vectors();
    init_device_mailboxes();

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
    use_mailbox(box, next_box_ID, num_slots, slot_size);
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
    int invalid_ID, position, status = 0;
    mailbox *box;
    mail_slot *slot;

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

    /* Okay, I think I finally got this.  NULL pointers are okay as
       long as the message size is 0*/
    if (!msg_ptr && (msg_size != 0))
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

    if (box->max_slots_count == 0)
        /* 0 slot mailbox special case */
        status = slotless_sender(box, msg_ptr, msg_size);
    else 
        status = slotful_sender(box, slot, msg_ptr, msg_size);

out:
    enableInterrupts();
    return status;
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
    int invalid_ID, position, status = 0;
    mailbox *box;
    mail_slot *slot;

    DP2(DEBUG3, "Receiving from mailbox %d into %08x with max of %d\n",
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
        status = slotless_receive(box, msg_ptr, msg_size);
    else 
        status = slotful_receive(box, slot, msg_ptr, msg_size);

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
        DP2(DEBUG5, "Front is %08x\n", queued);

        /* enables interrupts */
        ret = unblock_proc(queued->pid);
        if (ret != 0)
            KERNEL_ERROR("Unable to unblock process %d", queued->pid);

        DP2(DEBUG3,"Unblocked process %d\n", queued->pid);
        
        disableInterrupts();
        previous = queued;
        queued = queued->next;
        initialize_proc_entry(previous);
    }


    if (is_zapped())
        return -EZAPPED;

    enableInterrupts();
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
    {
        DP2(DEBUG, "Invalid mailbox ID %d", box_ID);
        return invalid;
    }

    box = MailBoxTable + position;

    /* 0 slot case */
    if (box->max_slots_count == 0)
    {
        if (!box->front)
            status = -EWOULDBLOCK;
        else if (box->front->type != PROCESS_RECEIVER)
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

    DP2(DEBUG4, "Currently using %d slots\n", slots_in_use);

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

    DP2(DEBUG, "running\n");

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
        {
            DP2(DEBUG, "Nothing on queue\n");
            status = -EWOULDBLOCK;
        }
        else if (box->front != PROCESS_SENDER)
        {
            DP2(DEBUG, "Thing on queue is not a sender\n");
            status = -EWOULDBLOCK;
        }
        else
        {
            status = MboxReceive(box_ID, msg_ptr, msg_max_size);
        }
    } else
    {
        /* slotful boxes */
        if (box->slots_count == 0)
        {
            DP2(DEBUG, "all slots empty: would block\n");
            status = -EWOULDBLOCK;
        }
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
    int box_ID, status;

    KERNEL_MODE_CHECK;
    disableInterrupts();

    if ((type < CLOCK_DEV) || (type > SYSCALL))
        KERNEL_ERROR("Invalid device type %d", type);

    DP2(DEBUG, "type == %d unit == %d\n", type, unit);

    box_ID = device_mbox_ID[type + unit]; 
    DP2(DEBUG, "type %d has mailbox %d\n", type, box_ID);
    status = MboxReceive(box_ID, device_status, sizeof(*device_status));

    if (is_zapped())
        status = -EWAITZAPPED;
    status = 0;

    enableInterrupts();

    return status;
}

