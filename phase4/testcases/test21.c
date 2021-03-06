#include <stdio.h>
#include <usloss.h>
#include <libuser.h>
#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usyscall.h>
#include <stdlib.h>
#include <string.h>


int Child1(char *arg)
{
   int term = atoi(arg);
   char buf[MAXLINE] = "";
   char buffer[MAXLINE];
   int read_length;
   int i, result, size;

   console("Child%d(): start\n", term);

   for (i = 0; i< 5; i++) {
      if (TermRead(buf, MAXLINE, term, &read_length) < 0) {
         console("ERROR: ReadTeam\n");
         return -1;
      }
      else {
         buf[read_length] = '\0';
         sprintf(buffer, "%s", buf);
         console("buffer written %s \n", buffer);
         result = TermWrite(buffer, strlen(buffer), term, &size);
         if (result < 0 || size != strlen(buffer)) {
             console("\n ***** Child(%d): got bad result or ", term);
             console("bad size! *****\n\n");
         }
      }
   }

   console("Child%d(): done\n", term);

   Terminate(0);
   return 0;
} /* Child 1 */


int start4(char *arg)
{
   int  pid, status, i;
   char buf[12];
   char child_buf[12];

   console("start4(): Spawn four children.\n");

   for (i=0; i<4; i++) {
      sprintf(buf, "%d", i);
      sprintf(child_buf, "Child%d", i);
      status = Spawn(child_buf, Child1, buf, USLOSS_MIN_STACK,2, &pid);
      assert(status == 0);
   }

   for (i=0; i<4; i++) {
      Wait(&pid, &status);
      assert(status == 0);
   }

   console("start4(): done.\n");
   Terminate(1);
   return 0;

} /* start4 */
