
TARGET=libphase2.a
ASSIGNMENT= 452phase2
CC=gcc
AR=ar
COBJS= phase2.o utility.o helper.o handler.o
CSRCS=${COBJS:.o=.c}
HDRS=message.h helper.h handler.h utility.h
#PHASE1LIB= patrickphase1debug
PHASE1LIB= patrickphase1
CFLAGS=-Wall -g2 -I. -I/home/cs452/spring05/include 
LDFLAGS += -L. -L/home/cs452/spring05/lib
TESTDIR=testcases
TESTS= test00 test01 test02 test03 test04 test05 test06 test07 test08 \
       test09 test10 test11 test12 test13 test14 test15 test16 test17 \
       test18 test19 test20 test21 test22
LIBS = -l$(PHASE1LIB) -lphase2 -lusloss -l$(PHASE1LIB)
TURNIN=Makefile phase2.c utility.c helper.c handler.c p1.c

$(TARGET):	$(COBJS)
		$(AR) -r $@ $(COBJS) 

$(TESTS):	$(TARGET) $(TESTDIR)/$$@.c p1.o
	$(CC) $(CFLAGS) -c $(TESTDIR)/$@.c
	$(CC) $(LDFLAGS) -o $@ $@.o $(LIBS) p1.o

clean:
	rm -f $(COBJS) $(TARGET) core term*.out test*.o $(TESTS) p1.o

handler.o: handler.c /home/cs452/spring05/include/phase1.h \
	   /home/cs452/spring05/include/usloss.h \
	   /home/cs452/spring05/include/solaris/machine.h \
	   /home/cs452/spring05/include/phase2.h utility.h handler.h
helper.o: helper.c helper.h message.h \
	  /home/cs452/spring05/include/phase2.h utility.h \
	 /home/cs452/spring05/include/usloss.h \
	 /home/cs452/spring05/include/solaris/machine.h handler.h \
	 /home/cs452/spring05/include/phase1.h \
	 /home/cs452/spring05/include/usloss.h
phase2.o: phase2.c /home/cs452/spring05/include/phase1.h \
	  /home/cs452/spring05/include/usloss.h \
	  /home/cs452/spring05/include/solaris/machine.h \
	  /home/cs452/spring05/include/phase2.h message.h utility.h helper.h
utility.o: utility.c utility.h /home/cs452/spring05/include/usloss.h \
	   /home/cs452/spring05/include/solaris/machine.h

turnin: $(CSRCS) $(HDRS) $(TURNIN)
	turnin $(ASSIGNMENT) $(CSRCS) $(HDRS) $(TURNIN)

check_turnin:
	turnin -ls $(ASSIGNMENT)

