TARGET=libphase1.a
ASSIGNMENT= 452phase1
CC=gcc
AR=ar
COBJS= phase1.o p1.o utility.o
CSRCS=${COBJS:.o=.c}
HDRS=kernel.h
CFLAGS= -Wall -g2 -I/home/cs452/spring05/include 
LDFLAGS += -L. -L/home/cs452/spring05/lib
TESTDIR=testcases
TESTS= test00 test01 test02 test03 test04 test05 test06 test07 test08 \
       test09 test10 test11 test12 test13 test14 test15 test16 test17 \
       test18 test19 test20 test21 test22 test23 test24 test25 test26
LIBS = -lphase1 -lusloss
TURNIN=README phase1.c p1.c utility.c utility.h kernel.h Makefile

$(TARGET):	$(COBJS)
		$(AR) -r $@ $(COBJS) 

$(TESTS):	$(TARGET) $(TESTDIR)/$$@.c
	$(CC) $(CFLAGS) -c $(TESTDIR)/$@.c
	$(CC) $(LDFLAGS) -o $@ $@.o $(LIBS)

clean:
	rm -f $(COBJS) $(TARGET) test?.o test??.o test? test?? \
		core term*.out p1.o

phase1.o:	kernel.h

turnin: $(CSRCS) $(HDRS) $(TURNIN)
	turnin $(ASSIGNMENT) $(CSRCS) $(HDRS) $(TURNIN)

check_turnin:
	turnin -ls $(ASSIGNMENT)

