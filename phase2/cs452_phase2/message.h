#ifndef MESSAGE_H
#define MESSAGE_H

#include <phase2.h>

/* Mailbox statuses */

#define MBOX_CLEARED             0
#define MBOX_BLOCKED_RECEIVE    51
#define MBOX_BLOCKED_SEND       52

typedef struct _mailbox mailbox;
typedef struct _mail_slot mail_slot;
typedef struct _proc_entry proc_entry;

typedef mail_slot *slot_ptr;
typedef mailbox mail_box;

typedef unsigned char byte_t;

/*typedef mbox_proc *mbox_proc_ptr;*/

struct _mailbox
{
    unsigned int mbox_ID;
    unsigned int max_message_size;
    unsigned int max_slots_count;
    unsigned int slots_count;
    /* Slots for this mailbox */
    mail_slot *slots_front, *slots_back;
    /* Queue of either senders or receivers that are blocked */
    proc_entry *front, *back;
};

struct _mail_slot
{
    mail_slot *next;
    int mbox_ID;    /* mailbox this slot is associated with */
    int status;     /* XXX: ??? */
    int pid;        /* so we know whom to unblock */
    int bytes;      /* bytes of data in 'data' */
    byte_t data[MAX_MESSAGE];
};

enum process_type { PROCESS_SENDER, PROCESS_RECEIVER, PROCESS_INVALID };

struct _proc_entry
{
    int pid;
    proc_entry *next;
    void *msg_ptr;
    int msg_size;
    enum process_type type;
};

struct psr_bits
{
   unsigned int unused:28;
   unsigned int prev_int_enable:1;
   unsigned int prev_mode:1;
   unsigned int cur_int_enable:1;
   unsigned int cur_mode:1;
};

union psr_values
{
   struct psr_bits bits;
   unsigned int integer_part;
};

#endif  /*  MESSAGE_H */
