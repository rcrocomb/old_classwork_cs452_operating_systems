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


int get_next_ID(void);
int find_empty_mailbox(void);
int is_valid_mailbox(int ID, int *position);
mail_slot *get_free_slot(mailbox *box);
mail_slot *get_next_slot(mailbox *box);
void free_slot(mailbox *box);
void add_to_slot_list(mailbox *box, mail_slot *slot);
void reinitialize_slot(mail_slot *s);
void initialize_mailbox(mailbox *box, int box_ID, int slots, int slot_size);
void reinitialize_mailbox(mailbox *box);
void enqueue(mailbox *box, int proc_entry);
proc_entry *dequeue(mailbox *box, int type);

int handle_enqueue_and_blocking(mailbox *box, const int blocking_type);

void init_vectors(void);
void init_device_handlers(void);

void reinitialize_proc_entry(proc_entry *p);

/* Required by phase 1 code */
int check_io(void);

#endif  /* HELPER_H */

