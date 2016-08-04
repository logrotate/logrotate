AM_CFLAGS = -Wall -Werror
sbin_PROGRAMS = logrotate
logrotate_SOURCES = basenames.c config.c log.c logrotate.c \
		    basenames.h config.h log.h logrotate.h queue.h

dist_man_MANS = logrotate.8 logrotate.conf.5

EXTRA_DIST = CHANGES README.md autogen.sh examples

# deprecated hard-wired Makefile, will be removed eventually
EXTRA_DIST += Makefile.legacy

# the dot ensures that logrotate is built before it is tested
SUBDIRS = . test

# needed for 'make dist' and 'make distcheck' to work out of the box
dist: all

# for compatibility with older releases of logrotate
test: check
