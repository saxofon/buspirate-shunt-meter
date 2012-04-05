#
# Copyright (C) 2012 Per Hallsmark <per@hallsmark.se>
#
# (see the files README and COPYING for more details)
#

CFLAGS += -g -ggdb -I../common

LDFLAGS += -lpthread
LDFLAGS += -lrt 

OBJS += ../common/buspirate.o
OBJS += ../common/ts.o

APPS += measure-shunt-current

all: $(APPS)

measure-shunt-current: $(OBJS)

clean:
	$(RM) $(APPS) $(OBJS)
