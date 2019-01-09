/*
 *  Author: Robert Crocombe
 *  Class: CS452 Spring 05
 *  Professor: Homer
 *  Initial Release: Tue Mar 29 21:09:03 MST 2005
 *
 *  Prototypes for kernel level functions to provide device
 *  functionality.  I wanted to put them in a header separate from
 *  user-level (syscall) functionality.
 */

#ifndef DRIVERS_H
#define DRIVERS_H

int clock_driver(char *arg);
int disk_driver(char *arg);
int terminal_driver(char *arg);

int terminal_receiver(char *arg);
int terminal_transmitter(char *arg);

#endif /* DRIVERS_H */
