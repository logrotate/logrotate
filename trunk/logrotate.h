#ifndef H_LOGROTATE
#define H_LOGROTATE

#include <sys/types.h>
#include <glob.h>

#define LOG_FLAG_COMPRESS	(1 << 0)
#define LOG_FLAG_CREATE		(1 << 1)
#define LOG_FLAG_IFEMPTY	(1 << 2)
#define LOG_FLAG_DELAYCOMPRESS	(1 << 3)
#define LOG_FLAG_COPYTRUNCATE	(1 << 4)
#define LOG_FLAG_MISSINGOK	(1 << 5)
#define LOG_FLAG_MAILFIRST	(1 << 6)

#define DEFAULT_MAIL_COMMAND "/bin/mail -s"
#define COMPRESS_COMMAND "gzip -9"
#define COMPRESS_EXT ".gz"
#define UNCOMPRESS_PIPE "gunzip"

#define NO_FORCE_ROTATE 0
#define FORCE_ROTATE    1

#define STATEFILE "/var/lib/logrotate.status"

typedef struct {
    char * pattern;
    char ** files;
    int numFiles;
    char * oldDir;
    enum { ROT_DAYS, ROT_WEEKLY, ROT_MONTHLY, ROT_SIZE, ROT_FORCE } criterium;
    unsigned int threshhold;
    int rotateCount;
    char * pre, * post;
    char * logAddress;
    char * errAddress;
    char * extension;
    int flags;
    mode_t createMode;		/* if any/all of these are -1, we use the */
    uid_t createUid;		/* attributes from the log file just rotated */
    gid_t createGid;
    glob_t globMem;		/* at least we could theoretically free this */
} logInfo;

int readConfigPath(char * path, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr);

extern int debug;

#endif
