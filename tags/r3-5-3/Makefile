VERSION = $(shell awk '/Version:/ { print $$2 }' logrotate.spec)
CVSTAG = r$(subst .,-,$(VERSION))
CFLAGS = -Wall -D_GNU_SOURCE -DVERSION=\"$(VERSION)\" $(RPM_OPT_FLAGS)
PROG = logrotate
BINDIR = /usr/sbin
MANDIR = /usr/man
MAN = logrotate.8
LOADLIBES = -lpopt

#--------------------------------------------------------------------------

OBJS = logrotate.o log.o config.o basenames.o
SOURCES = $(subst .o,.c,$(OBJS) $(LIBOBJS))

ifeq ($(RPM_OPT_FLAGS),)
CFLAGS += -g
LDFLAGS = -g
endif

ifeq (.depend,$(wildcard .depend))
TARGET=$(PROG)
else
TARGET=depend $(PROG)
endif

RCSVERSION = $(subst .,-,$(VERSION))

all: $(TARGET)

$(PROG): $(OBJS)

clean:
	rm -f $(OBJS) $(PROG) core*

depend:
	$(CPP) $(CFLAGS) -M $(SOURCES) > .depend

.PHONY : test
test:
	(cd test; ./test)

install:
	[ -d $(PREFIX)/$(BINDIR) ] || mkdir -p $(PREFIX)/$(BINDIR)
	[ -d $(PREFIX)/$(MANDIR) ] || mkdir -p $(PREFIX)/$(MANDIR)
	[ -d $(PREFIX)/$(MANDIR)/man8 ] || mkdir -p $(PREFIX)/$(MANDIR)/man8

	install -s -m 755 $(PROG) $(PREFIX)/$(BINDIR)
	install -m 644 $(MAN) $(PREFIX)/$(MANDIR)/man`echo $(MAN) | sed "s/.*\.//"`/$(MAN)

co:
	co RCS/*,v
	(cd examples; co RCS/*,v)

cvstag:
	cvs tag -F $(CVSTAG) .

archive: cvstag
	@rm -rf /tmp/logrotate-$(VERSION) /tmp/logrotate
	@cd /tmp; cvs export -r$(CVSTAG) logrotate; mv logrotate logrotate-$(VERSION)
	@cd /tmp/logrotate-$(VERSION)
	@cd /tmp; tar czSpf logrotate-$(VERSION).tar.gz logrotate-$(VERSION)
	@rm -rf /tmp/logrotate-$(VERSION)
	@cp /tmp/logrotate-$(VERSION).tar.gz .
	@rm -f /tmp/logrotate-$(VERSION).tar.gz
	@echo " "
	@echo "The final archive is ./logrotate-$(VERSION).tar.gz."

ifeq (.depend,$(wildcard .depend))
include .depend
endif
