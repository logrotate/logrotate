#ifndef H_LOGROTATE
#define H_LOGROTATE

#define LOG_FLAG_COMPRESS	(1 << 0)

#define COMPRESS_COMMAND "gzip -9"
#define COMPRESS_EXT ".gz"
#define UNCOMPRESS_PIPE "gunzip"

#define STATEFILE "/var/lib/logrotate.status"

typedef struct {
    char * fn;
    enum { ROT_DAYS, ROT_WEEKLY, ROT_MONTHLY, ROT_SIZE } criterium;
    unsigned int threshhold;
    int rotateCount;
    char * pre, * post;
    char * logAddress;
    char * errAddress;
    int flags;
} logInfo;

int readConfigPath(char * path, logInfo * defConfig, 
			  logInfo ** logsPtr, int * numLogsPtr);

extern int debug;

#endif
