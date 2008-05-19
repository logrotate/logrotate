VERSION = $(shell awk '/Version:/ { print $$2 }' logrotate.spec)
OS_NAME = $(shell uname -s)
LFS = $(shell echo `getconf LFS_CFLAGS 2>/dev/null`)
CFLAGS = -Wall -D_GNU_SOURCE -D$(OS_NAME) -DVERSION=\"$(VERSION)\" $(RPM_OPT_FLAGS) $(LFS)
PROG = logrotate
MAN = logrotate.8
LOADLIBES = -lpopt
SVNURL= svn+ssh://svn.fedorahosted.org/svn/logrotate
SVNPUBURL = http://svn.fedorahosted.org/svn/logrotate
SVNTAG = r$(subst .,-,$(VERSION))

ifeq ($(WITH_SELINUX),yes)
CFLAGS += -DWITH_SELINUX
LOADLIBES += -lselinux 
endif

# HP-UX using GCC
ifeq ($(OS_NAME),HP-UX)
    ifeq ($(RPM_OPT_FLAGS),)
        RPM_OPT_FLAGS = -O2
    endif
    CC = gcc
    INSTALL = cpset
    ifeq ($(POPT_DIR),)
        POPT_DIR = /usr/local
    endif
    ifeq ($(HPLX_DIR),)
	HPLX_DIR = /usr/local/hplx
    endif
    LOADLIBES += -lhplx -L$(HPLX_DIR)/lib
    ifeq ($(BASEDIR),)
	BASEDIR = /usr/local
    endif
endif

# Solaris using gcc
ifeq ($(OS_NAME),SunOS)
    ifeq ($(RPM_OPT_FLAGS),)
        RPM_OPT_FLAGS = -O2
    endif
    CC = gcc
    INSTALL = install
    ifeq ($(BASEDIR),)
	BASEDIR = /usr/local
    endif
endif

# Red Hat Linux
ifeq ($(OS_NAME),Linux)
    INSTALL = install
    BASEDIR = /usr
endif

ifneq ($(POPT_DIR),)
    CFLAGS += -I$(POPT_DIR)
    LOADLIBES += -L$(POPT_DIR)
endif

ifneq ($(STATEFILE),)
    CFLAGS += -DSTATEFILE=\"$(STATEFILE)\"
endif

BINDIR = $(BASEDIR)/sbin
MANDIR = $(BASEDIR)/man

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
	rm -f $(OBJS) $(PROG) core* .depend

depend:
	$(CPP) $(CFLAGS) -M $(SOURCES) > .depend

.PHONY : test
test: $(TARGET)
	(cd test; ./test)

install:
	[ -d $(PREFIX)/$(BINDIR) ] || mkdir -p $(PREFIX)/$(BINDIR)
	[ -d $(PREFIX)/$(MANDIR) ] || mkdir -p $(PREFIX)/$(MANDIR)
	[ -d $(PREFIX)/$(MANDIR)/man8 ] || mkdir -p $(PREFIX)/$(MANDIR)/man8

	if [ "$(OS_NAME)" = HP-UX ]; then \
	$(INSTALL) $(PROG) $(PREFIX)/$(BINDIR) 0755 bin bin; \
	$(INSTALL) $(MAN) $(PREFIX)/$(MANDIR)/man`echo $(MAN) | sed "s/.*\.//"` 0644 bin bin; \
	else \
	$(INSTALL) -m 755 $(PROG) $(PREFIX)/$(BINDIR); \
	$(INSTALL) -m 644 $(MAN) $(PREFIX)/$(MANDIR)/man`echo $(MAN) | sed "s/.*\.//"`/$(MAN); \
	fi

co:
	co RCS/*,v
	(cd examples; co RCS/*,v)

svntag:
	svn copy $(SVNURL)/trunk $(SVNURL)/tags/$(SVNTAG) -m "Release $(VERSION)"

create-archive:
	@rm -rf /tmp/logrotate-$(VERSION) /tmp/logrotate
	@cd /tmp; svn export $(SVNPUBURL)/tags/$(SVNTAG) logrotate-$(VERSION)
	@cd /tmp/logrotate-$(VERSION)
	@cd /tmp; tar czSpf logrotate-$(VERSION).tar.gz logrotate-$(VERSION)
	@rm -rf /tmp/logrotate-$(VERSION)
	@cp /tmp/logrotate-$(VERSION).tar.gz .
	@rm -f /tmp/logrotate-$(VERSION).tar.gz
	@echo " "
	@echo "The final archive is ./logrotate-$(VERSION).tar.gz."

archive: clean svntag create-archive

ifeq (.depend,$(wildcard .depend))
include .depend
endif
