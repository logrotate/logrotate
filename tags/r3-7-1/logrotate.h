#ifndef H_LOGROTATE
#define H_LOGROTATE

#include <sys/types.h>
#include <glob.h>

#include "config.h"

#define LOG_FLAG_COMPRESS	(1 << 0)
#define LOG_FLAG_CREATE		(1 << 1)
#define LOG_FLAG_IFEMPTY	(1 << 2)
#define LOG_FLAG_DELAYCOMPRESS	(1 << 3)
#define LOG_FLAG_COPYTRUNCATE	(1 << 4)
#define LOG_FLAG_MISSINGOK	(1 << 5)
#define LOG_FLAG_MAILFIRST	(1 << 6)
#define LOG_FLAG_SHAREDSCRIPTS	(1 << 7)
#define LOG_FLAG_COPY		(1 << 8)

#define NO_FORCE_ROTATE 0
#define FORCE_ROTATE    1

struct rotatePatternElement {
    enum { RP_NONE = 0, RP_FILENAME, RP_STRING, RP_COUNT, RP_MONTH, RP_DAY, 
	   RP_YEAR } type;
    char * arg;
    struct rotatePatternElement * next;
};

typedef struct {
    char * pattern;
    char ** files;
    int numFiles;
    char * oldDir;
    enum { ROT_DAYS, ROT_WEEKLY, ROT_MONTHLY, ROT_SIZE, ROT_FORCE } criterium;
    unsigned int threshhold;
    int rotateCount;
    int logStart;
    char * pre, * post, * first, * last;
    char * logAddress;
    char * extension;
    char * compress_prog;
    char * uncompress_prog;
    char * compress_ext;
    struct rotatePatternElement * rotatePattern;
    int flags;
    mode_t createMode;		/* if any/all of these are -1, we use the */
    uid_t createUid;		/* attributes from the log file just rotated */
    gid_t createGid;
    /* these are at the end so they end up nil */
    const char ** compress_options_list;
    int compress_options_count;
} logInfo;

int readConfigPath(const char * path, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr);

extern int debug;

#endif
