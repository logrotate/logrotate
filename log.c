#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef HAVE_VSYSLOG
#include <syslog.h>
#endif

#include "log.h"

int logLevel = MESS_DEBUG;
static FILE *errorFile = NULL;
static FILE *messageFile = NULL;
static int _logToSyslog = 0;
int flags = 0;

void logSetLevel(int level)
{
    logLevel = level;
}

void logSetErrorFile(FILE * f)
{
    errorFile = f;
}

void logSetMessageFile(FILE * f)
{
    messageFile = f;
}

void logToSyslog(int enable) {
	_logToSyslog = enable;

#ifdef HAVE_VSYSLOG
	if (_logToSyslog) {
		openlog("logrotate", 0, LOG_USER);
	}
	else {
		closelog();
	}
#endif
}

void logSetFlags(int newFlags)
{
    flags |= newFlags;
}

void logClearFlags(int newFlags)
{
    flags &= ~newFlags;
}

static void log_once(FILE *where, int level, char *format, va_list args)
{
	int showTime = 0;

	switch (level) {
	case MESS_DEBUG:
		showTime = 1;
		break;
	case MESS_NORMAL:
	case MESS_VERBOSE:
		break;
	default:
		if (flags & LOG_TIMES)
		fprintf(where, "%ld: ", (long) time(NULL));
		fprintf(where, "error: ");
		break;
	}

	if (showTime && (flags & LOG_TIMES)) {
		fprintf(where, "%ld:", (long) time(NULL));
	}

	vfprintf(where, format, args);
	fflush(where);
}

void message(int level, char *format, ...)
{
	va_list args;
    
	if (level >= logLevel) {
		va_start(args, format);
		log_once(stderr, level, format, args);
		va_end(args);
	}
    
	if (messageFile != NULL) {
		va_start(args, format);
		log_once(messageFile, level, format, args);
		va_end(args);
	}

#ifdef HAVE_VSYSLOG
	if (_logToSyslog) {
		int priority = LOG_USER;

		switch(level) {
			case MESS_REALDEBUG:
				priority |= LOG_DEBUG;
				break;
			case MESS_DEBUG:
			case MESS_VERBOSE:
			case MESS_NORMAL:
				priority |= LOG_INFO;
				break;
			case MESS_ERROR:
				priority |= LOG_ERR;
				break;
			case MESS_FATAL:
				priority |= LOG_CRIT;
				break;
			default:
				priority |= LOG_INFO;
				break;
		};

		va_start(args, format);
		vsyslog(priority, format, args);
		va_end(args);
	}
#endif

	if (level == MESS_FATAL)
		exit(1);
}
