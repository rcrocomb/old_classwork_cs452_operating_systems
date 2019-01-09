/*
 * Test Invalid disk and Terminal.
 */

#include <stdlib.h>
#include <string.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <usyscall.h>
#include <libuser.h>
#include <assert.h>
#include <stdio.h>


int start4(char *arg)
{
   int  status;
   char buffer[80];

   console("start4(): Attempt to write to a non-existant disk, disk 3\n");

   if (DiskWrite(buffer, 3, 1, 1, 1, &status) >= 0) {
      console("start4(): Disk : 1 Should not see this!!!\n");
   }

   if (DiskWrite(buffer, 0, 17, 1, 1, &status) >= 0) {
      console("start4(): Disk : 2 Should not see this!!!\n");
   }

   if (DiskWrite(buffer, 0, 1, 17, 1, &status) >= 0) {
      console("start4(): Disk : 3 Should not see this!!!\n");
   }

   console("start4(): done\n");

   Terminate(8);
   return 0;

} /* start4 */
