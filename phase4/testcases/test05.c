#include <stdio.h>
#include <libuser.h>
#include <usloss.h>
#include <phase4.h>
#include <strings.h>

int Child1(char *arg);
int Child2(char *arg);

int start4(char *arg)
{
   int kidpid, status;

   console("start4(): Spawn two children.  Child1 writes one line to\n");
   console("          terminal 1. Child2 reads one line from terminal 1.\n");
   Spawn("Child1", Child1, NULL, 2 * USLOSS_MIN_STACK, 4, &kidpid);
   Spawn("Child2", Child2, NULL, 2 * USLOSS_MIN_STACK, 4, &kidpid);
   Wait(&kidpid, &status);
   Wait(&kidpid, &status);
   console("start4(): done.\n");
   Terminate(0);

   console("start4(): should not see this message!\n");
   return 0;

} /* start4 */


int Child1(char *arg)
{
   char buffer[MAXLINE];
   int  result, size;

   sprintf(buffer, "A Something interesting to print here...\n");
   result = TermWrite(buffer, strlen(buffer)+1, 1, &size);

   console("Child1(): Terminating\n");
   Terminate(1);

   console("Child1(): should not see this message!\n");
   return 1;

} /* Child1 */

int Child2(char *arg)
{
   int  size;
   char buffer[MAXLINE];

   TermRead(buffer, MAXLINE, 1, &size);
   console("Child2(): read %d characters from terminal 1\n", size);
   console("Child2(): read `%s'\n", buffer);

   console("Child2(): Terminating\n");
   Terminate(1);

   console("Child2(): should not see this message!\n");
   return 1;

} /* Child2 */

