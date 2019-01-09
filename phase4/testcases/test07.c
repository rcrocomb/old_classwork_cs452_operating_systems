/* TERMTEST
 * Spawn off 4 children. The children read a line each from each terminal.
 */

#include <stdio.h>
#include <usloss.h>
#include <libuser.h>
#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usyscall.h>
#include <stdlib.h>


int Child(char *arg)
{
   int term = atoi(arg);
   char buf[80] = "";
   int len, read_length;

   console("Child%d(): start\n", term);

   len = 80; /* state the size of our buffer */

   if (TermRead(buf, len, term, &read_length) < 0) {
      console("Child%d(): ERROR -- TermRead\n", term);
      return -1;
   }
   else {
      buf[read_length] = '\0';
      console("Child%d(): read %s", term, buf);
      return 0;
   }

   console("Child%d(): done\n", term);

   Terminate(0);
   return 0;
} /* Child */


int start4(char *arg)
{
   int  pid, status, i;
   char buf[12];
   char child_buf[12];

   console("start4(): Spawn four children.  Each child reads fm a different\n");
   console("          terminal.  The child reading the shortest line will\n");
   console("          finish first, etc.\n");

   for (i = 0; i < 4; i++) {
      sprintf(buf, "%d", i);
      sprintf(child_buf, "Child%d", i);
      status = Spawn(child_buf, Child, buf, USLOSS_MIN_STACK, 4, &pid);
      assert(status == 0);
   }

   for (i = 0; i < 4; i++) {
      Wait(&pid, &status);
      assert(status == 0);
   }

   console("start4(): done.\n");
   Terminate(1);
   return 0;

} /* start4 */
