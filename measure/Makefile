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
