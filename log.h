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

#define message_OOM() \
    do { \
        message(MESS_ERROR, "can not allocate memory [%s():%d]\n", __func__, __LINE__); \
    } while(0)

#if 1
# define message_TRACE(tag) \
    do { \
        message(MESS_DEBUG, "TRACE: %s [%s():%d]\n", tag, __func__, __LINE__); \
    } while(0)
# define message_TRACE2(tag, arg) \
    do { \
        message(MESS_DEBUG, "TRACE: %s(%s) [%s():%d]\n", tag, arg, __func__, __LINE__); \
    } while(0)
#else
# define message_TRACE(tag)
# define message_TRACE2(tag, arg)
#endif

void logSetMessageFile(FILE * f);
void logFlushMessageFile(void);
void logToSyslog(int enable);
void logSetLevel(int level);

#endif

/* vim: set et sw=4 ts=4: */
