CFLAGS = -Wall -g -D_GNU_SOURCE
LDFLAGS = -g
OBJS = logrotate.o log.o

all: logrotate

logrotate: $(OBJS)

clean:
	rm -f $(OBJS) logrotate core
