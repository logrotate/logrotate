MAN = logrotate.8
MAN5 = logrotate.conf.5

AM_CFLAGS = -Wall -Werror
sbin_PROGRAMS = logrotate
logrotate_SOURCES = logrotate.c log.c config.c basenames.c

install-data-local:
	$(AM_V_at)$(INSTALL) -d $(DESTDIR)$(mandir)/man8
	$(AM_V_at)$(INSTALL) -d $(DESTDIR)$(mandir)/man5
	$(AM_V_at)$(INSTALL) -m 644 @srcdir@/logrotate.8 $(DESTDIR)$(mandir)/man8
	$(AM_V_at)$(INSTALL) -m 644 @srcdir@/logrotate.conf.5 $(DESTDIR)$(mandir)/man5
uninstall-local:
	$(AM_V_at)rm -f $(DESTDIR)$(mandir)/man8/logrotate.8
	$(AM_V_at)rm -f $(DESTDIR)$(mandir)/man5/logrotate.conf.5

SUBDIRS = test
