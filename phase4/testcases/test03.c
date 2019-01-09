/* TERMTEST
 * Send a negative length to TermRead. Send an invalid terminal
 * number to TermWrite. The routines should return -1.
 */

#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <usyscall.h>
#include <libuser.h>
#include <assert.h>


int start4(char *arg)
{
  int len,j;
  char a[13];
  char b[13] = "abcdefghijklm";

  console("start4(): Read a negative number of characters from terminal 1.\n");
  console("          Write to terminal -1.  Should get back a -1 from both\n");
  console("          operations since they have invalid arguments.\n");
  j = TermRead(a, -13, 1, &len);
  assert(j == -1);
  j = TermWrite(b, 13, -1, &len);
  assert(j == -1);
  console("start4(): Done with test of illegal terminal parameters: %d %d\n",
          i, j);
  Terminate(3);

  return 0;
} /* start4 */
