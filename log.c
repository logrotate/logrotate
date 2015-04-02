#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.h"

int logLevel = MESS_DEBUG;
static FILE *errorFile = NULL;
static FILE *messageFile = NULL;
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

	if (level == MESS_FATAL)
		exit(1);
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
}
