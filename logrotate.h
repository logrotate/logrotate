#ifndef H_LOGROTATE
#define H_LOGROTATE

#include <sys/types.h>
#include "queue.h"
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
#define LOG_FLAG_DATEEXT	(1 << 9)
#define LOG_FLAG_SHRED		(1 << 10)
#define LOG_FLAG_SU			(1 << 11)
#define LOG_FLAG_DATEYESTERDAY	(1 << 12)
#define LOG_FLAG_OLDDIRCREATE	(1 << 13)
#define LOG_FLAG_TMPFILENAME	(1 << 14)

#define NO_MODE ((mode_t) -1)
#define NO_UID  ((uid_t) -1)
#define NO_GID  ((gid_t) -1)

#define NO_FORCE_ROTATE 0
#define FORCE_ROTATE    1

#ifdef HAVE_LIBSELINUX
#define WITH_SELINUX 1
#endif

#ifdef HAVE_LIBACL
#define WITH_ACL 1
#endif

struct logInfo {
    char *pattern;
    char **files;
    int numFiles;
    char *oldDir;
    enum { ROT_HOURLY, ROT_DAYS, ROT_WEEKLY, ROT_MONTHLY, ROT_YEARLY, ROT_SIZE
            } criterium;
    unsigned long long threshhold;
	unsigned long long maxsize;
    unsigned long long minsize;
    int rotateCount;
    int rotateAge;
    int logStart;
    char *pre, *post, *first, *last, *preremove;
    char *logAddress;
    char *extension;
    char *compress_prog;
    char *uncompress_prog;
    char *compress_ext;
	char *dateformat;		/* specify format for strftime (for dateext) */
    int flags;
	int shred_cycles;		/* if !=0, pass -n shred_cycles to GNU shred */
    mode_t createMode;		/* if any/all of these are -1, we use the */
    uid_t createUid;		/* attributes from the log file just rotated */
    gid_t createGid;
    uid_t suUid;			/* switch user to this uid and group to this gid */
    gid_t suGid;
    mode_t olddirMode;
    uid_t olddirUid;
    uid_t olddirGid;
    /* these are at the end so they end up nil */
    const char **compress_options_list;
    int compress_options_count;
    TAILQ_ENTRY(logInfo) list;
};

TAILQ_HEAD(logInfoHead, logInfo) logs;

extern int numLogs;
extern int debug;

int readAllConfigPaths(const char **paths);
#if !defined(asprintf) && !defined(_FORTIFY_SOURCE)
int asprintf(char **string_ptr, const char *format, ...);
#endif

#endif
