#ifndef HELPER_H
#define HELPER_H

#include "message.h"

#define ENOIDS  1
#define ENOBOX 1
#define ESLOTSIZE 1
#define EBADBOX 1
#define EMSGSIZE 1
#define ENULLMSG 1
#define EWAITZAPPED 1

#define EWOULDBLOCK 2
#define ENOSLOTS 2

#define EZAPPED 3
#define EBOXRELEASED 3

#define RECEIVER 1
#define SENDER 2
#define EITHER 4

#define EMPTY_BOX_ID -1


//#define CLEAR_PROC_INFO set_process_entry_info(NULL, 0, PROCESS_INVALID)
#define CLEAR_PROC_INFO (0)

int get_next_ID(void);
int find_empty_mailbox(void);
int is_valid_mailbox(int ID, int *position);
mail_slot *get_free_slot(mailbox *box);
mail_slot *get_next_slot(mailbox *box);
void release_slot(mailbox *box);
void add_to_slot_list(mailbox *box, mail_slot *slot);
void handle_message_copy(mailbox *box, mail_slot *slot, void *msg_ptr, int msg_size);
void handle_pending_senders(mailbox *box);
void initialize_slot(mail_slot *s);
void use_mailbox(mailbox *box, int box_ID, int slots, int slot_size);
void initialize_mailbox(mailbox *box);
void reinitialize_mailbox(mailbox *box);
void enqueue(mailbox *box, proc_entry *p);
proc_entry *dequeue(mailbox *box, const enum process_type type);

int handle_enqueue_and_blocking(mailbox *box, void *msg_ptr, int msg_size, const enum process_type type);

void init_vectors(void);
void init_device_mailboxes(void);

void initialize_proc_entry(proc_entry *p);

void set_process_entry_info(void *msg_ptr, int msg_size, const enum process_type type);

void initialize_mailbox_table(void);
void initialize_slot_table(void);

/* Required by phase 1 code */
int check_io(void);

void release_process(mailbox *box, const enum process_type type);

int slotless_sender(mailbox *box, void *msg_ptr, int msg_size);
int slotful_sender(mailbox *box, mail_slot *slot, void *msg_ptr, int msg_size);
int slotless_receive(mailbox *box, void *msg_ptr, int msg_size);
int slotful_receive(mailbox *box, mail_slot *slot, void *msg_ptr, int msg_size);

#endif  /* HELPER_H */

