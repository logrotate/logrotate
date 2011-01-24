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

#if 0
void log(int fd, char *format, ...)
{
    int i = 0;
    char *buf = NULL;
    va_list args;
    int size;

    va_start(args, format);

    do {
	i += 1000;
	if (buf)
	    free(buf);
	buf = malloc(i);
	size = vsnprintf(buf, i, format, args);
    } while (size >= i);

    write(fd, buf, size);

    free(buf);

    va_end(args);
}
#endif

void message(int level, char *format, ...)
{
    va_list args;
    FILE *where = NULL;
    int showTime = 0;

    if (errorFile == NULL)
	errorFile = stderr;
    if (messageFile == NULL)
	messageFile = stderr;
    where = errorFile;

    if (level >= logLevel) {
	va_start(args, format);

	switch (level) {
	case MESS_DEBUG:
	    where = messageFile;
	    showTime = 1;
	    break;

	case MESS_NORMAL:
	case MESS_VERBOSE:
	    where = messageFile;
	    break;

	default:
	    if (flags & LOG_TIMES)
		fprintf(where, "%ld: ", (long) time(NULL));
	    fprintf(errorFile, "error: ");
	    break;
	}

	if (showTime && (flags & LOG_TIMES)) {
	    fprintf(where, "%ld:", (long) time(NULL));
	}

	vfprintf(where, format, args);
	fflush(where);

	va_end(args);

	if (level == MESS_FATAL)
	    exit(1);
    }
}
