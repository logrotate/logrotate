#include <stdarg.h>
#include <stdio.h>

#include "log.h"

int logLevel = LOG_DEBUG;

void message(int level, char * format, ...) {
    va_list args;
    FILE * where = stderr;

    if (level >= logLevel) {
	va_start(args, format);

	switch (level) {
	    case LOG_DEBUG:
		where = stdout;
		break;

	    case LOG_NORMAL:
	    case LOG_VERBOSE:
		where = stdout;
		break;

	    default:
		fprintf(stderr, "error: ");
	}

	vfprintf(where, format, args);

	if (level == LOG_FATAL) exit(1);
    }
}


