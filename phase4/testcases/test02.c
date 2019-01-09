/* TERMTEST
   Test reading from a terminal which doesn't have as many bytes as we ask for.
*/

#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <usyscall.h>
#include <libuser.h>
#include <assert.h>
#include <stdio.h>
#include <strings.h>


int XXterm2(char *arg);


int start4(char *arg)
{
  int kidpid, pid, status;

  console("start4(): started\n");

  status=Spawn("XXterm2", XXterm2, NULL, USLOSS_MIN_STACK, 4, &kidpid);
  assert(status==0);

  Wait(&pid, &status);
  console("start4(): XXterm2 completed. kidpid = %d, pid = %d\n", kidpid, pid);

  Terminate(5);
  return 0;
} /* start4 */


int XXterm2(char *arg)
{
  int j, len;
  char data[256];

  console("XXterm2(): started\n");

  /* line 00, don't have as many bytes as we ask for */
  len = 0;
  bzero(data, 256);
  if (TermRead(data, 80, 2, &len) < 0) { /* ask for 80 bytes */
    console("XXterm2(): ERROR: TermRead\n");
  }

  console("XXterm2(): after TermRead()\n");
  console("XXterm2(): term2 read %d bytes: ", len);
  for (j = 0; j < strlen(data); j++) {
    console("%c", data[j]);
  }
  /* don't print a newline here, as it should be read into data already */

  console("XXterm2(): Terminating\n");
  Terminate(4);

  return 0;
} /* XXterm2 */

