#ifndef H_LOG
#define H_LOG

#include <stdio.h>

#define MESS_REALDEBUG  1
#define MESS_DEBUG      2
#define MESS_VERBOSE    3
#define MESS_NORMAL     4
#define MESS_ERROR      5
#define MESS_FATAL      6

#define LOG_TIMES       (1 << 0)

void message(int level, const char *format, ...)
#ifdef __GNUC__
    __attribute__ ((format(printf, 2, 3)));
#else
;
#endif
void logSetMessageFile(FILE * f);
void logToSyslog(int enable);
void logSetLevel(int level);

#endif

/* vim: set et sw=4 ts=4: */
