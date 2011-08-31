VERSION = $(shell awk '/Version:/ { print $$2 }' logrotate.spec)
OS_NAME = $(shell uname -s)
LFS = $(shell echo `getconf LFS_CFLAGS 2>/dev/null`)
CFLAGS = -Wall -D_GNU_SOURCE -D$(OS_NAME) -DVERSION=\"$(VERSION)\" $(RPM_OPT_FLAGS) $(LFS)
PROG = logrotate
MAN = logrotate.8
MAN5 = logrotate.conf.5
LOADLIBES = -lpopt
SVNURL= svn+ssh://svn.fedorahosted.org/svn/logrotate
SVNPUBURL = http://svn.fedorahosted.org/svn/logrotate
SVNTAG = r$(subst .,-,$(VERSION))

ifeq ($(WITH_SELINUX),yes)
CFLAGS += -DWITH_SELINUX
LOADLIBES += -lselinux
endif

ifeq ($(WITH_ACL),yes)
CFLAGS += -DWITH_ACL
LOADLIBES += -lacl
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
    CFLAGS = -D_GNU_SOURCE -D$(OS_NAME) -DVERSION=\"$(VERSION)\" $(RPM_OPT_FLAGS) $(LFS)
    CC ?= gcc
    CPP = $(CC) -E -M
    INSTALL = /usr/ucb/install
    ifeq ($(CC),cc)
        CPP = cc -xM
    endif
    BASEDIR ?= /usr/local
endif

# Red Hat Linux
ifeq ($(OS_NAME),Linux)
    INSTALL = install
    BASEDIR = /usr
endif

# FreeBSD
ifeq ($(OS_NAME),FreeBSD)
    LOADLIBES += -L${LOCALBASE}/lib
    CFLAGS += -I${LOCALBASE}/include
    PREFIX=
endif

ifeq ($(OS_NAME),NetBSD)
    CFLAGS += -I/usr/include
    CFLAGS += -I$(BASEDIR)/include
    LOADLIBES += -L/usr/lib
    LOADLIBES += -L$(BASEDIR)/lib -Wl,-R,$(BASEDIR)/lib
endif

ifneq ($(POPT_DIR),)
    CFLAGS += -I$(POPT_DIR)
    LOADLIBES += -L$(POPT_DIR)
endif

ifneq ($(STATEFILE),)
    CFLAGS += -DSTATEFILE=\"$(STATEFILE)\"
endif

BINDIR = $(BASEDIR)/sbin
MANDIR ?= $(BASEDIR)/man

#--------------------------------------------------------------------------

OBJS = logrotate.o log.o config.o basenames.o
SOURCES = $(subst .o,.c,$(OBJS) $(LIBOBJS))

ifeq ($(RPM_OPT_FLAGS),)
CFLAGS += -g
LDFLAGS = -g
endif

LDFLAGS += $(EXTRA_LDFLAGS) $(EXTRA_LIBS)
CFLAGS  += $(EXTRA_CPPFLAGS) $(EXTRA_CFLAGS) 

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
	[ -d $(PREFIX)/$(MANDIR)/man5 ] || mkdir -p $(PREFIX)/$(MANDIR)/man5

	if [ "$(OS_NAME)" = HP-UX ]; then \
	$(INSTALL) $(PROG) $(PREFIX)/$(BINDIR) 0755 bin bin; \
	$(INSTALL) $(MAN) $(PREFIX)/$(MANDIR)/man`echo $(MAN) | sed "s/.*\.//"` 0644 bin bin; \
	$(INSTALL) $(MAN5) $(PREFIX)/$(MANDIR)/man`echo $(MAN5) | sed "s/.*\.//"` 0644 bin bin; \
	else if [ "$(OS_NAME)" = FreeBSD ]; then \
	$(BSD_INSTALL_PROGRAM) $(PROG) $(BINDIR); \
	$(BSD_INSTALL_MAN) $(MAN) $(MANDIR)/man`echo $(MAN) | sed "s/.*\.//"`/$(MAN); \
	$(BSD_INSTALL_MAN) $(MAN5) $(MANDIR)/man`echo $(MAN5) | sed "s/.*\.//"`/$(MAN5); \
	else \
	$(INSTALL) -m 755 $(PROG) $(PREFIX)/$(BINDIR); \
	$(INSTALL) -m 644 $(MAN) $(PREFIX)/$(MANDIR)/man`echo $(MAN) | sed "s/.*\.//"`/$(MAN); \
	$(INSTALL) -m 644 $(MAN5) $(PREFIX)/$(MANDIR)/man`echo $(MAN5) | sed "s/.*\.//"`/$(MAN5); \
	fi; fi

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
