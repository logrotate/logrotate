MAN = logrotate.8
MAN5 = logrotate.conf.5

AM_CFLAGS = -Wall -Werror
sbin_PROGRAMS = logrotate
logrotate_SOURCES = basenames.c config.c log.c logrotate.c \
		    basenames.h config.h log.h logrotate.h queue.h

EXTRA_DIST = autogen.sh

install-data-local:
	$(AM_V_at)$(INSTALL) -d $(DESTDIR)$(mandir)/man8
	$(AM_V_at)$(INSTALL) -d $(DESTDIR)$(mandir)/man5
	$(AM_V_at)$(INSTALL) -m 644 @srcdir@/logrotate.8 $(DESTDIR)$(mandir)/man8
	$(AM_V_at)$(INSTALL) -m 644 @srcdir@/logrotate.conf.5 $(DESTDIR)$(mandir)/man5
uninstall-local:
	$(AM_V_at)rm -f $(DESTDIR)$(mandir)/man8/logrotate.8
	$(AM_V_at)rm -f $(DESTDIR)$(mandir)/man5/logrotate.conf.5

SUBDIRS = test
