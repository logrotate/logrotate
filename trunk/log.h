#ifndef H_LOG
#define H_LOG

#define LOG_REALDEBUG	1
#define LOG_DEBUG	2
#define LOG_VERBOSE	3
#define LOG_NORMAL	4
#define LOG_ERROR	5
#define LOG_FATAL	6

void message(int level, char * format, ...);

#endif
