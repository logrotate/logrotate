VERSION = 2.4
CFLAGS = -Wall -D_GNU_SOURCE -DVERSION=\"$(VERSION)\" $(RPM_OPT_FLAGS)
PROG = logrotate
BINDIR = /usr/sbin
MANDIR = /usr/man
MAN = logrotate.8

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

install:
	[ -d $(PREFIX)/$(BINDIR) ] || mkdir -p $(PREFIX)/$(BINDIR)
	[ -d $(PREFIX)/$(MANDIR) ] || mkdir -p $(PREFIX)/$(MANDIR)
	[ -d $(PREFIX)/$(MANDIR)/man8 ] || mkdir -p $(PREFIX)/$(MANDIR)/man8

	install -s -m 755 $(PROG) $(PREFIX)/$(BINDIR)
	install -m 644 $(MAN) $(PREFIX)/$(MANDIR)/man`echo $(MAN) | sed "s/.*\.//"`/$(MAN)

co:
	co RCS/*,v
	(cd examples; co RCS/*,v)

rcstag:
	rcs -q -N$(RCSVERSION): RCS/*,v
	(cd examples; rcs -q -N$(RCSVERSION): RCS/*,v)

archive: rcstag
	@rm -rf /tmp/$(PROG)-$(VERSION)
	@mkdir /tmp/$(PROG)-$(VERSION)
	@rm -rf /tmp/$(PROG)-$(VERSION)
	@mkdir /tmp/$(PROG)-$(VERSION)
	@tar cSpf - * | (cd /tmp/$(PROG)-$(VERSION); tar xSpf -)
	@cd /tmp/$(PROG)-$(VERSION); \
	    make clean; \
	    find . -name "RCS" -exec rm {} \;  ; \
	    find . -name ".depend" -exec rm {} \;  ; \
	    rm -rf *gz test* *.tar foo* shared \;
	@cd /tmp; tar czSpf $(PROG)-$(VERSION).tar.gz $(PROG)-$(VERSION)
	@rm -rf /tmp/$(PROG)-$(VERSION)
	@cp /tmp/$(PROG)-$(VERSION).tar.gz .
	@rm -f /tmp/$(PROG)-$(VERSION).tar.gz
	@echo " "
	@echo "The final archive is ./$(PROG)-$(VERSION).tar.gz."

ifeq (.depend,$(wildcard .depend))
include .depend
endif
