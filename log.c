#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.h"

int logLevel = MESS_DEBUG;
static FILE * errorFile = stderr;
static FILE * messageFile = stderr;

void logSetErrorFile(FILE * f) {
    errorFile = f;
}

void logSetMessageFile(FILE * f) {
    messageFile = f;
}

void log(int fd, char * format, ...) {
    int i = 0;
    char * buf = NULL;
    va_list args;
    int size = -1;

    va_start(args, format);

    do {
	i += 1000;
	if (buf) free(buf);
	buf = malloc(i);
	size = vsnprintf(buf, size, format, args);
    } while (i == size);

    write(fd, buf, size);

    free(buf);

    va_end(args);
}

void message(int level, char * format, ...) {
    va_list args;
    FILE * where = errorFile;
    int showTime = 0;

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
		fprintf(errorFile, "error: ");
		showTime = 1;
	}

	if (showTime) {
	    fprintf(where, "%ld:", (long) time(NULL));
	}

	vfprintf(where, format, args);

	va_end(args);

	if (level == MESS_FATAL) exit(1);
    }
}


