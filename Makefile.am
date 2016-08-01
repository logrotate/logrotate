AM_CFLAGS = -Wall -Werror
sbin_PROGRAMS = logrotate
logrotate_SOURCES = basenames.c config.c log.c logrotate.c \
		    basenames.h config.h log.h logrotate.h queue.h

dist_man_MANS = logrotate.8 logrotate.conf.5

SUBDIRS = test
