/* TERMTEST
 * Read exactly 13 bytes from term 1. Display the bytes to stdout.
 */

#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <usyscall.h>
#include <libuser.h>
#include <assert.h>
#include <stdio.h>
#include <strings.h>

char buf[256];

int start4(char *arg)
{
  int j, length;
  char data[256];

  console("start4(): Read bytes from terminal 1, but ask for fewer bytes\n");
  console("          than are present on the first line.\n");

  length = 0;
  bzero(data, 256);

  if (TermRead(data, 13, 1, &length) < 0) {
    console("start4(): ERROR from Readterm\n");
    Terminate(0);
  }
/*
  if (length == 1 && data[0] == '\n') {
    if (TermRead(data, 13, 0, &length) < 0) {
      console(" ERROR: TermRead\n");
      Terminate(0);
    }
  }
*/

  console("start4(): term1 read %d bytes, first 13 bytes: `", length);
  console(buf);
  for (j = 0; j < 13; j++)
    console("%c", data[j]);
  console("'\n");

  console("start4(): simple terminal test is done.\n");

  Terminate(3);

  return 0;

} /* start4 */
