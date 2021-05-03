#include "queue.h"
/* alloca() is defined in stdlib.h in NetBSD */
#if !defined(__NetBSD__) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
#include <alloca.h>
#endif
#include <limits.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <popt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <glob.h>
#include <locale.h>
#include <sys/types.h>
#include <utime.h>
#include <stdint.h>
#include <libgen.h>

#if !defined(PATH_MAX) && defined(__FreeBSD__)
#include <sys/param.h>
#endif

#include "log.h"
#include "logrotate.h"

static char *prev_context;
#ifdef WITH_SELINUX
#include <selinux/selinux.h>
static int selinux_enabled = 0;
static int selinux_enforce = 0;
#endif

#ifdef WITH_ACL
#include "sys/acl.h"
#define acl_type acl_t
#else
#define acl_type void *
#endif

static acl_type prev_acl = NULL;

#if !defined(GLOB_ABORTED) && defined(GLOB_ABEND)
#define GLOB_ABORTED GLOB_ABEND
#endif

#ifdef PATH_MAX
#define STATEFILE_BUFFER_SIZE ( 2 * PATH_MAX + 16 )
#else
#define STATEFILE_BUFFER_SIZE 4096
#endif

#ifdef __hpux
extern int asprintf(char **str, const char *fmt, ...);
#endif

/* Number of seconds in a day */
#define DAY_SECONDS 86400

struct logState {
    char *fn;
    struct tm lastRotated;  /* only tm_hour, tm_mday, tm_mon, tm_year are good! */
    struct stat sb;
    int doRotate;
    int isUsed;     /* True if there is real log file in system for this state. */
    LIST_ENTRY(logState) list;
};

struct logNames {
    char *firstRotated;
    char *disposeName;
    char *finalName;
    char *dirName;
    char *baseName;
};

struct compData {
    size_t prefix_len;
    const char *dformat;
};

static struct logStateList {
    LIST_HEAD(stateSet, logState) head;
} **states;

int numLogs = 0;
int debug = 0;

static unsigned int hashSize;
static const char *mailCommand = DEFAULT_MAIL_COMMAND;
static time_t nowSecs = 0;
static uid_t save_euid;
static gid_t save_egid;

static int globerr(const char *pathname, int theerr)
{
    message(MESS_ERROR, "error accessing %s: %s\n", pathname,
            strerror(theerr));

    /* We want the glob operation to abort on error, so return 1 */
    return 1;
}

#if defined(HAVE_STRPTIME) && defined(HAVE_QSORT)

/* We could switch to qsort_r to get rid of this global variable,
 * but qsort_r is not portable enough (Linux vs. *BSD vs ...)... */
static struct compData _compData;

static int compGlobResult(const void *result1, const void *result2)  {
    struct tm time_tmp;
    time_t t1, t2;
    const char *r1 = *(char * const*)(result1);
    const char *r2 = *(char * const*)(result2);

    memset(&time_tmp, 0, sizeof(struct tm));
    strptime(r1 + _compData.prefix_len, _compData.dformat, &time_tmp);
    t1 = mktime(&time_tmp);

    memset(&time_tmp, 0, sizeof(struct tm));
    strptime(r2 + _compData.prefix_len, _compData.dformat, &time_tmp);
    t2 = mktime(&time_tmp);

    if (t1 < t2) return -1;
    if (t1 > t2) return  1;
    return 0;
}

static void sortGlobResult(glob_t *result, size_t prefix_len, const char *dformat) {
    if (!dformat || *dformat == '\0') {
        return;
    }

    _compData.prefix_len = prefix_len;
    _compData.dformat = dformat;
    qsort(result->gl_pathv, result->gl_pathc, sizeof(char *), compGlobResult);
}
#else
static void sortGlobResult(glob_t *result, size_t prefix_len, const char *dformat) {
    /* TODO */
}
#endif

int switch_user(uid_t user, gid_t group) {
    save_egid = getegid();
    save_euid = geteuid();
    if (save_euid == user && save_egid == group)
        return 0;
    message(MESS_DEBUG, "switching euid from %u to %u and egid from %u to %u (pid %d)\n",
            (unsigned) save_euid, (unsigned) user, (unsigned) save_egid, (unsigned) group, getpid());
    if (setegid(group) || seteuid(user)) {
        message(MESS_ERROR, "error switching euid from %u to %u and egid from %u to %u (pid %d): %s\n",
                (unsigned) save_euid, (unsigned) user, (unsigned) save_egid, (unsigned) group, getpid(),
                strerror(errno));
        return 1;
    }
    return 0;
}

static int switch_user_permanently(const struct logInfo *log) {
    const gid_t group = getegid();
    const uid_t user = geteuid();

    if (!(log->flags & LOG_FLAG_SU)) {
        return 0;
    }

    if (user != log->suUid) {
        message(MESS_ERROR, "current euid (%u) does not match uid of log configuration (%u) (pid %d)\n",
                (unsigned) user, (unsigned) log->suUid, getpid());
        return 1;
    }
    if (group != log->suGid) {
        message(MESS_ERROR, "current egid (%u) does not match gid of log configuration (%u) (pid %d)\n",
                (unsigned) group, (unsigned) log->suGid, getpid());
        return 1;
    }

    /* we are already the final configuration specified user/group */
    if (getuid() == user && getgid() == group) {
        return 0;
    }

    /* switch to full root first */
    if (setgid(getgid()) || setuid(getuid())) {
        message(MESS_ERROR, "error getting rid of euid != uid (pid %d): %s\n",
                getpid(), strerror(errno));
        return 1;
    }

    message(MESS_DEBUG, "switching uid to %u and gid to %u permanently (pid %d)\n",
            (unsigned) user, (unsigned) group, getpid());
    if (setgid(group) || setuid(user)) {
        message(MESS_ERROR, "error switching uid to %u and gid to %u (pid %d): %s\n",
                (unsigned) user, (unsigned) group, getpid(), strerror(errno));
        return 1;
    }

    if (user != ROOT_UID && setuid(ROOT_UID) != -1) {
        message(MESS_ERROR, "failed to switch user permanently, able to switch back (pid %d)\n",
                getpid());
        return 1;
    }

    return 0;
}

int switch_user_back(void) {
    return switch_user(save_euid, save_egid);
}

static int switch_user_back_permanently(void) {
    gid_t tmp_egid = save_egid;
    uid_t tmp_euid = save_euid;
    int ret = switch_user(save_euid, save_egid);
    save_euid = tmp_euid;
    save_egid = tmp_egid;
    return ret;
}

static void unescape(char *arg)
{
    char *p = arg;
    char *next;
    char escaped;
    while ((next = strchr(p, '\\')) != NULL) {

        p = next;

        switch (p[1]) {
            case 'n':
                escaped = '\n';
                break;
            case '\\':
                escaped = '\\';
                break;
            default:
                ++p;
                continue;
        }

        /* Overwrite the backslash with the intended character,
         * and shift everything down one */
        *p++ = escaped;
        memmove(p, p+1, 1 + strlen(p+1));
    }
}

#define HASH_SIZE_MIN 64
#define HASH_SIZE_MAX 8192
static int allocateHash(unsigned long hs)
{
    unsigned int i;

    /* Enforce some reasonable minimum hash size */
    if (hs < HASH_SIZE_MIN)
        hs = HASH_SIZE_MIN;

    /* Enforce some reasonable maximum hash size */
    if (hs > HASH_SIZE_MAX)
        hs = HASH_SIZE_MAX;

    message(MESS_DEBUG, "Allocating hash table for state file, size %lu entries\n",
            hs);

    states = calloc(hs, sizeof(struct logStateList *));
    if (states == NULL) {
        message_OOM();
        return 1;
    }

    for (i = 0; i < hs; i++) {
        states[i] = malloc(sizeof *states[0]);
        if (states[i] == NULL) {
            message_OOM();
            return 1;
        }
        LIST_INIT(&(states[i]->head));
    }

    hashSize = (unsigned)hs;

    return 0;
}

#define HASH_CONST 13
#if defined(__clang__) && defined(__clang_major__) && (__clang_major__ >= 4)
__attribute__((no_sanitize("unsigned-integer-overflow")))
#endif
static int hashIndex(const char *fn)
{
    unsigned hash = 0;
    if (!hashSize)
        /* hash table not yet allocated */
        return -1;

    while (*fn) {
        hash *= HASH_CONST;
        hash += (unsigned char)*fn++;
    }

    return (int)(hash % hashSize);
}

/* safe implementation of dup2(oldfd, nefd) followed by close(oldfd) */
static int movefd(int oldfd, int newfd)
{
    int rc;
    if (oldfd == newfd)
        /* avoid accidental close of newfd in case it is equal to oldfd */
        return 0;

    rc = dup2(oldfd, newfd);
    if (rc == 0)
        close(oldfd);

    return rc;
}

static int setSecCtx(int fdSrc, const char *src, char **pPrevCtx)
{
#ifdef WITH_SELINUX
    char *srcCtx;
    *pPrevCtx = NULL;

    if (!selinux_enabled)
        /* pretend success */
        return 0;

    /* read security context of fdSrc */
    if (fgetfilecon_raw(fdSrc, &srcCtx) < 0) {
        if (errno == ENOTSUP)
            /* pretend success */
            return 0;

        message(MESS_ERROR, "getting file context %s: %s\n", src,
                strerror(errno));
        return selinux_enforce;
    }

    /* save default security context for restoreSecCtx() */
    if (getfscreatecon_raw(pPrevCtx) < 0) {
        message(MESS_ERROR, "getting default context: %s\n", strerror(errno));
        return selinux_enforce;
    }

    /* set default security context to match fdSrc */
    if (setfscreatecon_raw(srcCtx) < 0) {
        message(MESS_ERROR, "setting default context to %s: %s\n", srcCtx,
                strerror(errno));
        freecon(srcCtx);
        return selinux_enforce;
    }

    message(MESS_DEBUG, "set default create context to %s\n", srcCtx);
    freecon(srcCtx);
#else
    (void) fdSrc;
    (void) src;
    (void) pPrevCtx;
#endif
    return 0;
}

static int setSecCtxByName(const char *src, char **pPrevCtx)
{
    int hasErrors = 0;
#ifdef WITH_SELINUX
    int fd = open(src, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        message(MESS_ERROR, "error opening %s: %s\n", src, strerror(errno));
        return 1;
    }
    hasErrors = setSecCtx(fd, src, pPrevCtx);
    close(fd);
#else
    (void) src;
    (void) pPrevCtx;
#endif
    return hasErrors;
}

static void restoreSecCtx(char **pPrevCtx)
{
#ifdef WITH_SELINUX
    if (!*pPrevCtx)
        /* no security context saved for restoration */
        return;

    /* set default security context to the previously stored one */
    if (selinux_enabled && setfscreatecon_raw(*pPrevCtx) < 0)
        message(MESS_ERROR, "setting default context to %s: %s\n", *pPrevCtx,
                strerror(errno));

    /* free the memory allocated to save the security context */
    freecon(*pPrevCtx);
    *pPrevCtx = NULL;
#else
    (void) pPrevCtx;
#endif
}

static struct logState *newState(const char *fn)
{
    struct tm now;
    struct logState *new;
    time_t lr_time;

    message(MESS_DEBUG, "Creating new state\n");

    localtime_r(&nowSecs, &now);

    new = malloc(sizeof(*new));
    if (new == NULL) {
        message_OOM();
        return NULL;
    }

    new->fn = strdup(fn);
    if (new->fn  == NULL) {
        message_OOM();
        free(new);
        return NULL;
    }

    new->doRotate = 0;
    new->isUsed = 0;

    memset(&new->lastRotated, 0, sizeof(new->lastRotated));
    new->lastRotated.tm_hour = now.tm_hour;
    new->lastRotated.tm_mday = now.tm_mday;
    new->lastRotated.tm_mon = now.tm_mon;
    new->lastRotated.tm_year = now.tm_year;
    new->lastRotated.tm_isdst = now.tm_isdst;

    /* fill in the rest of the new->lastRotated fields */
    lr_time = mktime(&new->lastRotated);
    localtime_r(&lr_time, &new->lastRotated);

    return new;
}

static struct logState *findState(const char *fn)
{
    const int i = hashIndex(fn);
    struct logState *p;
    if (i < 0)
        /* hash table not yet allocated */
        return NULL;

    for (p = states[i]->head.lh_first; p != NULL; p = p->list.le_next)
        if (!strcmp(fn, p->fn))
            break;

    /* new state */
    if (p == NULL) {
        if ((p = newState(fn)) == NULL)
            return NULL;

        LIST_INSERT_HEAD(&(states[i]->head), p, list);
    }

    return p;
}

static int runScript(const struct logInfo *log, const char *logfn, const char *logrotfn, const char *script)
{
    int rc;
    pid_t pid;

    if (debug) {
        message(MESS_DEBUG, "running script with args %s %s: \"%s\"\n",
                logfn, logrotfn ? logrotfn : "", script);
        return 0;
    }

    pid = fork();

    if (pid == -1) {
        message(MESS_ERROR, "cannot fork: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0) {
        if (log->flags & LOG_FLAG_SU) {
            if (switch_user_back_permanently() != 0) {
                exit(1);
            }
        }
        execl("/bin/sh", "sh", "-c", (char *) script, "logrotate_script", (char *) logfn, (char *) logrotfn, (char *) NULL);
        message(MESS_ERROR, "cannot execute sub-shell: %s\n", strerror(errno));
        exit(1);
    }

    wait(&rc);
    return rc;
}

#ifdef WITH_ACL
static int is_acl_well_supported(int err)
{
    switch (err) {
        case ENOTSUP:   /* no file system support */
        case EINVAL:    /* acl does not point to a valid ACL */
        case ENOSYS:    /* compatibility - acl_(g|s)et_fd(3) should never return this */
        case EBUSY:     /* compatibility - acl_(g|s)et_fd(3) should never return this */
            return 0;
        default:
            return 1;
    }
}
#endif /* WITH_ACL */

static int createOutputFile(const char *fileName, int flags, const struct stat *sb,
                            acl_type acl, int force_mode)
{
    int fd = -1;
    struct stat sb_create;
    int acl_set = 0;
    int i;

    for (i = 0; i < 2; ++i) {
        struct tm now;
        size_t fileName_size, buf_size;
        char *backupName, *ptr;

        fd = open(fileName, (flags | O_EXCL | O_NOFOLLOW),
                (S_IRUSR | S_IWUSR) & sb->st_mode);

        if ((fd >= 0) || (errno != EEXIST))
            break;

        /* the destination file already exists, while it should not */
        localtime_r(&nowSecs, &now);
        fileName_size = strlen(fileName);
        buf_size = fileName_size + sizeof("-YYYYMMDDHH.backup");
        backupName = alloca(buf_size);
        ptr = backupName;

        /* construct backupName starting with fileName */
        strcpy(ptr, fileName);
        ptr += fileName_size;
        buf_size -= fileName_size;

        /* append the -YYYYMMDDHH time stamp and the .backup suffix */
        ptr += strftime(ptr, buf_size, "-%Y%m%d%H", &now);
        strcpy(ptr, ".backup");

        message(MESS_ERROR, "destination %s already exists, renaming to %s\n",
                fileName, backupName);
        if (rename(fileName, backupName) != 0) {
            message(MESS_ERROR, "error renaming already existing output file"
                    " %s to %s: %s\n", fileName, backupName, strerror(errno));
            return -1;
        }
        /* existing file renamed, try it once again */
    }

    if (fd < 0) {
        message(MESS_ERROR, "error creating output file %s: %s\n",
                fileName, strerror(errno));
        return -1;
    }
    if (fchmod(fd, (S_IRUSR | S_IWUSR) & sb->st_mode)) {
        message(MESS_ERROR, "error setting mode of %s: %s\n",
                fileName, strerror(errno));
        close(fd);
        return -1;
    }

    if (fstat(fd, &sb_create)) {
        message(MESS_ERROR, "fstat of %s failed: %s\n", fileName,
                strerror(errno));
        close(fd);
        return -1;
    }

    /* Only attempt to set user/group if running as root */
    if (
        ROOT_UID == geteuid() &&
        (sb_create.st_uid != sb->st_uid || sb_create.st_gid != sb->st_gid) &&
        fchown(fd, sb->st_uid, sb->st_gid)
    ) {
        message(MESS_ERROR, "error setting owner of %s to uid %u and gid %u: %s\n",
                fileName, (unsigned) sb->st_uid, (unsigned) sb->st_gid, strerror(errno));
        close(fd);
        return -1;
    }

#ifdef WITH_ACL
    if (!force_mode && acl) {
        if (acl_set_fd(fd, acl) == -1) {
            if (is_acl_well_supported(errno)) {
                message(MESS_ERROR, "setting ACL for %s: %s\n",
                        fileName, strerror(errno));
                close(fd);
                return -1;
            }
            acl_set = 0;
        }
        else {
            acl_set = 1;
        }
    }
#else
    (void) acl;
#endif

    if (!acl_set || force_mode) {
        if (fchmod(fd, sb->st_mode)) {
            message(MESS_ERROR, "error setting mode of %s: %s\n",
                    fileName, strerror(errno));
            close(fd);
            return -1;
        }
    }

    return fd;
}

#define DIGITS 12

/* unlink, but try to call shred from GNU coreutils if LOG_FLAG_SHRED
 * is enabled (in that case fd needs to be a valid file descriptor) */
static int shred_file(int fd, const char *filename, const struct logInfo *log)
{
    char count[DIGITS];    /*  that's a lot of shredding :)  */
    const char **fullCommand;
    int id = 0;
    int status;
    pid_t pid;

    if (log->preremove) {
        message(MESS_DEBUG, "running preremove script\n");
        if (runScript(log, filename, NULL, log->preremove)) {
            message(MESS_ERROR,
                    "error running preremove script "
                    "for %s of '%s'. Not removing this file.\n",
                    filename, log->pattern);
            /* What ever was supposed to happen did not happen,
             * therefore do not unlink the file yet.  */
            return 1;
        }
    }

    if (!(log->flags & LOG_FLAG_SHRED)) {
        goto unlink_file;
    }

    message(MESS_DEBUG, "Using shred to remove the file %s\n", filename);

    if (log->shred_cycles != 0) {
        fullCommand = alloca(sizeof(*fullCommand) * 6);
    }
    else {
        fullCommand = alloca(sizeof(*fullCommand) * 4);
    }
    fullCommand[id++] = "shred";
    fullCommand[id++] = "-u";

    if (log->shred_cycles != 0) {
        fullCommand[id++] = "-n";
        snprintf(count, DIGITS - 1, "%d", log->shred_cycles);
        fullCommand[id++] = count;
    }
    fullCommand[id++] = "-";
    fullCommand[id++] = NULL;

    pid = fork();

    if (pid == -1) {
        message(MESS_ERROR, "cannot fork: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0) {
        movefd(fd, STDOUT_FILENO);

        if (switch_user_permanently(log) != 0) {
            exit(1);
        }

        execvp(fullCommand[0], (void *) fullCommand);
        message(MESS_ERROR, "cannot execute shred command: %s\n", strerror(errno));
        exit(1);
    }

    wait(&status);

    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        message(MESS_ERROR, "Failed to shred %s, trying unlink\n", filename);
        return unlink(filename);
    }

    /* We have to unlink it after shred anyway,
     * because it doesn't remove the file itself */

unlink_file:
    if (unlink(filename) == 0)
        return 0;
    if (errno != ENOENT)
        return 1;

    /* unlink of log file that no longer exists is not a fatal error */
    message(MESS_ERROR, "error unlinking log file %s: %s\n", filename,
            strerror(errno));
    return 0;
}

static int removeLogFile(const char *name, const struct logInfo *log)
{
    int fd = -1;
    int result = 0;
    message(MESS_DEBUG, "removing old log %s\n", name);

    if (log->flags & LOG_FLAG_SHRED) {
        fd = open(name, O_RDWR | O_NOFOLLOW);
        if (fd < 0) {
            message(MESS_ERROR, "error opening %s: %s\n",
                    name, strerror(errno));
            return 1;
        }
    }

    if (!debug && shred_file(fd, name, log)) {
        message(MESS_ERROR, "Failed to remove old log %s: %s\n",
                name, strerror(errno));
        result = 1;
    }

    if (fd != -1)
        close(fd);
    return result;
}

static void setAtimeMtime(const char *filename, const struct stat *sb)
{
    /* If we can't change atime/mtime, it's not a disaster.  It might
       possibly fail under SELinux. But do try to preserve the
       fractional part if we have utimensat(). */
#if defined HAVE_UTIMENSAT && !defined(__APPLE__)
    struct timespec ts[2];

    ts[0] = sb->st_atim;
    ts[1] = sb->st_mtim;
    utimensat(AT_FDCWD, filename, ts, 0);
#else
    struct utimbuf utim;

    utim.actime = sb->st_atime;
    utim.modtime = sb->st_mtime;
    utime(filename, &utim);
#endif
}

static int compressLogFile(const char *name, const struct logInfo *log, const struct stat *sb)
{
    char *compressedName;
    const char **fullCommand;
    int inFile;
    int outFile;
    int i;
    int status;
    int compressPipe[2];
    char buff[4092];
    ssize_t n_read;
    int error_printed = 0;
    char *prevCtx;
    pid_t pid;
    int in_flags;
    const char *in_how;

    message(MESS_DEBUG, "compressing log with: %s\n", log->compress_prog);
    if (debug)
        return 0;

    fullCommand = alloca(sizeof(*fullCommand) *
            ((unsigned)log->compress_options_count + 2));
    fullCommand[0] = log->compress_prog;
    for (i = 0; i < log->compress_options_count; i++)
        fullCommand[i + 1] = log->compress_options_list[i];
    fullCommand[log->compress_options_count + 1] = NULL;

    compressedName = alloca(strlen(name) + strlen(log->compress_ext) + 2);
    sprintf(compressedName, "%s%s", name, log->compress_ext);

    in_flags = O_NOFOLLOW;
    if (log->flags & LOG_FLAG_SHRED) {
        /* need write access for shredding */
        in_flags |= O_RDWR;
        in_how = "read-write";
    } else {
        in_flags |= O_RDONLY;
        in_how = "read-only";
    }
    if ((inFile = open(name, in_flags)) < 0) {
        message(MESS_ERROR, "unable to open %s (%s) for compression: %s\n",
            name, in_how, strerror(errno));
        return 1;
    }

    if (setSecCtx(inFile, name, &prevCtx) != 0) {
        /* error msg already printed */
        close(inFile);
        return 1;
    }

#ifdef WITH_ACL
    if ((prev_acl = acl_get_fd(inFile)) == NULL) {
        if (is_acl_well_supported(errno)) {
            message(MESS_ERROR, "getting file ACL %s: %s\n",
                    name, strerror(errno));
            restoreSecCtx(&prevCtx);
            close(inFile);
            return 1;
        }
    }
#endif

    outFile =
        createOutputFile(compressedName, O_RDWR | O_CREAT, sb, prev_acl, 0);
    restoreSecCtx(&prevCtx);
#ifdef WITH_ACL
    if (prev_acl) {
        acl_free(prev_acl);
        prev_acl = NULL;
    }
#endif
    if (outFile < 0) {
        close(inFile);
        return 1;
    }

    /* pipe used to capture stderr of the compress process */
    if (pipe(compressPipe) < 0) {
        message(MESS_ERROR, "error opening pipe for compress: %s\n",
                strerror(errno));
        close(inFile);
        close(outFile);
        return 1;
    }

    pid = fork();

    if (pid == -1) {
        message(MESS_ERROR, "cannot fork: %s\n", strerror(errno));
        close(inFile);
        close(outFile);
        close(compressPipe[1]);
        close(compressPipe[0]);
        return 1;
    }

    if (pid == 0) {
        char *envInFilename;

        /* close read end of pipe in the child process */
        close(compressPipe[0]);

        movefd(inFile, STDIN_FILENO);
        movefd(outFile, STDOUT_FILENO);

        if (switch_user_permanently(log) != 0) {
            exit(1);
        }

        movefd(compressPipe[1], STDERR_FILENO);

        envInFilename = alloca(strlen("LOGROTATE_COMPRESSED_FILENAME=") + strlen(name) + 2);
        sprintf(envInFilename, "LOGROTATE_COMPRESSED_FILENAME=%s", name);
        putenv(envInFilename);
        execvp(fullCommand[0], (void *) fullCommand);
        message(MESS_ERROR, "cannot execute compress command: %s\n", strerror(errno));
        exit(1);
    }

    /* close write end of pipe in the parent process */
    close(compressPipe[1]);

    while ((n_read = read(compressPipe[0], buff, sizeof(buff) - 1)) > 0) {
        if (!error_printed) {
            error_printed = 1;
            message(MESS_ERROR, "Compressing program wrote following message "
                    "to stderr when compressing log %s:\n", name);
        }
        buff[n_read] = '\0';
        fprintf(stderr, "%s", buff);
    }
    close(compressPipe[0]);
    wait(&status);

    fsync(outFile);
    close(outFile);

    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        message(MESS_ERROR, "failed to compress log %s\n", name);
        close(inFile);
        unlink(compressedName);
        return 1;
    }

    setAtimeMtime(compressedName, sb);

    if (shred_file(inFile, name, log)) {
        close(inFile);
        return 1;
    }

    close(inFile);

    return 0;
}

static int mailLog(const struct logInfo *log, const char *logFile, const char *mailComm,
                   const char *uncompressCommand, const char *address, const char *subject)
{
    int mailInput;
    pid_t mailChild, uncompressChild = 0;
    int mailStatus, uncompressStatus;
    int uncompressPipe[2];
    char * const mailArgv[] = { (char *) mailComm, (char *) "-s", (char *) subject, (char *) address, NULL };
    int rc = 0;

    if ((mailInput = open(logFile, O_RDONLY | O_NOFOLLOW)) < 0) {
        message(MESS_ERROR, "failed to open %s for mailing: %s\n", logFile,
                strerror(errno));
        return 1;
    }

    if (uncompressCommand) {
        /* pipe used to capture output of the uncompress process */
        if (pipe(uncompressPipe) < 0) {
            message(MESS_ERROR, "error opening pipe for uncompress: %s\n",
                    strerror(errno));
            close(mailInput);
            return 1;
        }

        uncompressChild = fork();

        if (uncompressChild == -1) {
            message(MESS_ERROR, "cannot fork: %s\n", strerror(errno));
            close(mailInput);
            close(uncompressPipe[1]);
            close(uncompressPipe[0]);
            return 1;
        }

        if (uncompressChild == 0) {
            /* uncompress child */

            /* close read end of pipe in the child process */
            close(uncompressPipe[0]);

            movefd(mailInput, STDIN_FILENO);
            movefd(uncompressPipe[1], STDOUT_FILENO);

            if (switch_user_permanently(log) != 0) {
                exit(1);
            }

            execlp(uncompressCommand, uncompressCommand, (char *) NULL);
            message(MESS_ERROR, "cannot execute uncompress command: %s\n", strerror(errno));
            exit(1);
        }

        close(mailInput);
        mailInput = uncompressPipe[0];
        close(uncompressPipe[1]);
    }

    mailChild = fork();

    if (mailChild == -1) {
        message(MESS_ERROR, "cannot fork: %s\n", strerror(errno));
        close(mailInput);
        return 1;
    }

    if (mailChild == 0) {
        movefd(mailInput, STDIN_FILENO);
        close(STDOUT_FILENO);

        /* mail command runs as root */
        if (log->flags & LOG_FLAG_SU) {
            if (switch_user_back_permanently() != 0) {
                exit(1);
            }
        }

        execvp(mailArgv[0], mailArgv);
        message(MESS_ERROR, "cannot execute mail command: %s\n", strerror(errno));
        exit(1);
    }

    close(mailInput);

    waitpid(mailChild, &mailStatus, 0);

    if (!WIFEXITED(mailStatus) || WEXITSTATUS(mailStatus)) {
        message(MESS_ERROR, "mail command failed for %s\n", logFile);
        rc = 1;
    }

    if (uncompressCommand) {
        waitpid(uncompressChild, &uncompressStatus, 0);

        if (!WIFEXITED(uncompressStatus) || WEXITSTATUS(uncompressStatus)) {
            message(MESS_ERROR, "uncompress command failed mailing %s\n",
                    logFile);
            rc = 1;
        }
    }

    return rc;
}

static int mailLogWrapper(const char *mailFilename, const char *mailComm,
                          unsigned logNum, const struct logInfo *log)
{
    /* uncompress already compressed log files before mailing them */
    const char *uncompress_prog = (log->flags & LOG_FLAG_COMPRESS)
        ? log->uncompress_prog
        : NULL;

    const char *subject = mailFilename;
    if (log->flags & LOG_FLAG_MAILFIRST) {
        if (log->flags & LOG_FLAG_DELAYCOMPRESS)
            /* the log we are mailing has not been compressed yet */
            uncompress_prog = NULL;

        if (uncompress_prog)
            /* use correct subject when mailfirst is enabled */
            subject = log->files[logNum];
    }

    return mailLog(log, mailFilename, mailComm, uncompress_prog,
                   log->logAddress, subject);
}

/* Use a heuristic to determine whether stat buffer SB comes from a file
   with sparse blocks.  If the file has fewer blocks than would normally
   be needed for a file of its size, then at least one of the blocks in
   the file is a hole.  In that case, return true.  */
static int is_probably_sparse(struct stat const *sb)
{
#if defined(HAVE_STRUCT_STAT_ST_BLOCKS) && defined(HAVE_STRUCT_STAT_ST_BLKSIZE)
    return (S_ISREG (sb->st_mode)
            && sb->st_blksize != 0
            && sb->st_blocks < sb->st_size / sb->st_blksize);
#else
    return 0;
#endif
}

#define MIN(a,b) ((a) < (b) ? (a) : (b))

/* Return whether the buffer consists entirely of NULs.
   Note the word after the buffer must be non NUL. */

static int is_nul (void const *buf, size_t bufsize)
{
    char const *cbuf = buf;
    char const *cp = buf;

    /* Find the first nonzero *byte*, or the sentinel.  */
    while (*cp++ == 0)
        continue;

    return cbuf + bufsize < cp;
}

static size_t full_write(int fd, const void *buf, size_t count)
{
    size_t total = 0;
    const char *ptr = (const char *) buf;

    while (count > 0)
    {
        size_t n_rw;
        for (;;)
        {
            n_rw = (size_t)write (fd, buf, count);
            if (errno == EINTR)
                continue;
            else
                break;
        }
        if (n_rw == (size_t) -1)
            break;
        if (n_rw == 0)
            break;
        total += n_rw;
        ptr += n_rw;
        count -= n_rw;
    }

    return total;
}

static int sparse_copy(int src_fd, int dest_fd, const struct stat *sb,
                       const char *saveLog, const char *currLog)
{
    const int make_holes = is_probably_sparse(sb);
    size_t max_n_read = SIZE_MAX;
    int last_write_made_hole = 0;
    off_t total_n_read = 0;
    char buf[BUFSIZ + 1];

    while (max_n_read) {
        int make_hole = 0;
        size_t bytes_read;
        const ssize_t n_read = read (src_fd, buf, MIN (max_n_read, BUFSIZ));
        if (n_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            message(MESS_ERROR, "error reading %s: %s\n",
                    currLog, strerror(errno));
            return 0;
        }

        if (n_read == 0)
            break;

        bytes_read = (size_t)n_read;

        max_n_read -= bytes_read;
        total_n_read += n_read;

        if (make_holes) {
            /* Sentinel required by is_nul().  */
            buf[bytes_read] = '\1';

            if ((make_hole = is_nul(buf, bytes_read))) {
                if (lseek (dest_fd, n_read, SEEK_CUR) < 0) {
                    message(MESS_ERROR, "error seeking %s: %s\n",
                            saveLog, strerror(errno));
                    return 0;
                }
            }
        }

        if (!make_hole) {
            if (full_write (dest_fd, buf, bytes_read) != bytes_read) {
                message(MESS_ERROR, "error writing to %s: %s\n",
                        saveLog, strerror(errno));
                return 0;
            }
        }

        last_write_made_hole = make_hole;
    }

    if (last_write_made_hole) {
        if (ftruncate(dest_fd, total_n_read) < 0) {
            message(MESS_ERROR, "error ftruncate %s: %s\n",
                    saveLog, strerror(errno));
            return 0;
        }
    }

    return 1;
}

static int copyTruncate(const char *currLog, const char *saveLog, const struct stat *sb,
                        int flags, int skip_copy)
{
    int rc = 1;
    int fdcurr = -1, fdsave = -1;

    message(MESS_DEBUG, "copying %s to %s\n", currLog, saveLog);

    if (!debug) {
        /* read access is sufficient for 'copy' but not for 'copytruncate' */
        const int read_only = (flags & LOG_FLAG_COPY)
            && !(flags & LOG_FLAG_COPYTRUNCATE);
        if ((fdcurr = open(currLog, ((read_only) ? O_RDONLY : O_RDWR) | O_NOFOLLOW)) < 0) {
            message(MESS_ERROR, "error opening %s: %s\n", currLog,
                    strerror(errno));
            goto fail;
        }

        if (!skip_copy) {
            char *prevCtx;

            if (setSecCtx(fdcurr, currLog, &prevCtx) != 0) {
                /* error msg already printed */
                goto fail;
            }
#ifdef WITH_ACL
            if ((prev_acl = acl_get_fd(fdcurr)) == NULL) {
                if (is_acl_well_supported(errno)) {
                    message(MESS_ERROR, "getting file ACL %s: %s\n",
                            currLog, strerror(errno));
                    restoreSecCtx(&prevCtx);
                    goto fail;
                }
            }
#endif /* WITH_ACL */
            fdsave = createOutputFile(saveLog, O_WRONLY | O_CREAT, sb, prev_acl, 0);
            restoreSecCtx(&prevCtx);
#ifdef WITH_ACL
            if (prev_acl) {
                acl_free(prev_acl);
                prev_acl = NULL;
            }
#endif
            if (fdsave < 0)
                goto fail;

            if (sparse_copy(fdcurr, fdsave, sb, saveLog, currLog) != 1) {
                message(MESS_ERROR, "error copying %s to %s: %s\n", currLog,
                        saveLog, strerror(errno));
                unlink(saveLog);
                goto fail;
            }
        }
    }

    if (flags & LOG_FLAG_COPYTRUNCATE) {
        message(MESS_DEBUG, "truncating %s\n", currLog);

        if (!debug) {
            if (fdsave >= 0)
                fsync(fdsave);
            if (ftruncate(fdcurr, 0)) {
                message(MESS_ERROR, "error truncating %s: %s\n", currLog,
                        strerror(errno));
                goto fail;
            }
        }
    } else
        message(MESS_DEBUG, "Not truncating %s\n", currLog);

    rc = 0;
fail:
    if (fdcurr >= 0) {
        close(fdcurr);
    }
    if (fdsave >= 0) {
        close(fdsave);
    }
    return rc;
}

/* return value similar to mktime() but the exact time is ignored */
static time_t mktimeFromDateOnly(const struct tm *src)
{
    /* explicit struct copy to retain C89 compatibility */
    struct tm tmp;
    memcpy(&tmp, src, sizeof tmp);

    /* abstract out (nullify) fields expressing the exact time */
    tmp.tm_hour = 0;
    tmp.tm_min  = 0;
    tmp.tm_sec  = 0;
    return mktime(&tmp);
}

/* return by how many days the date was advanced but ignore exact time */
static time_t daysElapsed(const struct tm *now, const struct tm *last)
{
    const time_t diff = mktimeFromDateOnly(now) - mktimeFromDateOnly(last);
    return diff / (24 * 3600);
}

static int findNeedRotating(const struct logInfo *log, unsigned logNum, int force)
{
    struct stat sb;
    struct logState *state = NULL;
    struct tm now;

    message(MESS_DEBUG, "considering log %s\n", log->files[logNum]);

    localtime_r(&nowSecs, &now);

    /* Check if parent directory of this log has safe permissions */
    if ((log->flags & LOG_FLAG_SU) == 0 && getuid() == 0) {
        char *ld;
        char *logpath = strdup(log->files[logNum]);
        if (logpath == NULL) {
            message_OOM();
            return 1;
        }
        ld = dirname(logpath);
        if (stat(ld, &sb)) {
            /* If parent directory doesn't exist, it's not real error
               (unless nomissingok is specified)
               and rotation is not needed */
            if (errno != ENOENT || (log->flags & LOG_FLAG_MISSINGOK) == 0) {
                message(MESS_ERROR, "stat of %s failed: %s\n", ld,
                        strerror(errno));
                free(logpath);
                return 1;
            }
            free(logpath);
            return 0;
        }
        /* Don't rotate in directories writable by others or group which is not "root"  */
        if ((sb.st_gid != 0 && (sb.st_mode & S_IWGRP)) || (sb.st_mode & S_IWOTH)) {
            message(MESS_ERROR, "skipping \"%s\" because parent directory has insecure permissions"
                    " (It's world writable or writable by group which is not \"root\")"
                    " Set \"su\" directive in config file to tell logrotate which user/group"
                    " should be used for rotation.\n"
                    ,log->files[logNum]);
            free(logpath);
            return 1;
        }
        free(logpath);
    }

    if (lstat(log->files[logNum], &sb)) {
        if ((log->flags & LOG_FLAG_MISSINGOK) && (errno == ENOENT)) {
            message(MESS_DEBUG, "  log %s does not exist -- skipping\n",
                    log->files[logNum]);
            return 0;
        }
        message(MESS_ERROR, "stat of %s failed: %s\n", log->files[logNum],
                strerror(errno));
        return 1;
    }

    state = findState(log->files[logNum]);
    if (!state)
        return 1;

    state->doRotate = 0;
    state->sb = sb;
    state->isUsed = 1;

    if ((sb.st_mode & S_IFMT) == S_IFLNK) {
        message(MESS_DEBUG, "  log %s is symbolic link. Rotation of symbolic"
                " links is not allowed to avoid security issues -- skipping.\n",
                log->files[logNum]);
        return 0;
    }

    message(MESS_DEBUG, "  Now: %d-%02d-%02d %02d:%02d\n", 1900 + now.tm_year,
            1 + now.tm_mon, now.tm_mday,
            now.tm_hour, now.tm_min);

    message(MESS_DEBUG, "  Last rotated at %d-%02d-%02d %02d:%02d\n", 1900 + state->lastRotated.tm_year,
            1 + state->lastRotated.tm_mon, state->lastRotated.tm_mday,
            state->lastRotated.tm_hour, state->lastRotated.tm_min);

    if (force) {
        /* user forced rotation of logs from command line */
        state->doRotate = 1;
    }
    else if (log->maxsize && sb.st_size > log->maxsize) {
        state->doRotate = 1;
    }
    else if (log->criterium == ROT_SIZE) {
        state->doRotate = (sb.st_size >= log->threshold);
        if (!state->doRotate) {
            message(MESS_DEBUG, "  log does not need rotating "
                    "(log size is below the 'size' threshold)\n");
        }
    } else if (mktime(&state->lastRotated) - mktime(&now) > (25 * 3600)) {
        /* 25 hours allows for DST changes as well as geographical moves */
        message(MESS_ERROR,
                "log %s last rotated in the future -- rotation forced\n",
                log->files[logNum]);
        state->doRotate = 1;
    } else if (state->lastRotated.tm_year != now.tm_year ||
            state->lastRotated.tm_mon != now.tm_mon ||
            state->lastRotated.tm_mday != now.tm_mday ||
            state->lastRotated.tm_hour != now.tm_hour) {
        time_t days;
        switch (log->criterium) {
            case ROT_WEEKLY:
                days = daysElapsed(&now, &state->lastRotated);
                /* rotate if date is advanced by 7+ days (exact time is ignored) */
                state->doRotate = (days >= 7)
                    /* ... or if we have not yet rotated today */
                    || (days >= 1
                            /* ... and the selected weekday is today */
                            && (unsigned)now.tm_wday == log->weekday);
                if (!state->doRotate) {
                    message(MESS_DEBUG, "  log does not need rotating "
                            "(log has been rotated at %d-%02d-%02d %02d:%02d, "
                            "which is less than a week ago)\n", 1900 + state->lastRotated.tm_year,
                            1 + state->lastRotated.tm_mon, state->lastRotated.tm_mday,
                            state->lastRotated.tm_hour, state->lastRotated.tm_min);
                }
                break;
            case ROT_HOURLY:
                state->doRotate = ((now.tm_hour != state->lastRotated.tm_hour) ||
                        (now.tm_mday != state->lastRotated.tm_mday) ||
                        (now.tm_mon != state->lastRotated.tm_mon) ||
                        (now.tm_year != state->lastRotated.tm_year));
                if (!state->doRotate) {
                    message(MESS_DEBUG, "  log does not need rotating "
                            "(log has been rotated at %d-%02d-%02d %02d:%02d, "
                            "which is less than an hour ago)\n", 1900 + state->lastRotated.tm_year,
                            1 + state->lastRotated.tm_mon, state->lastRotated.tm_mday,
                            state->lastRotated.tm_hour, state->lastRotated.tm_min);
                }
                break;
            case ROT_DAYS:
                state->doRotate = ((now.tm_mday != state->lastRotated.tm_mday) ||
                        (now.tm_mon != state->lastRotated.tm_mon) ||
                        (now.tm_year != state->lastRotated.tm_year));
                if (!state->doRotate) {
                    message(MESS_DEBUG, "  log does not need rotating "
                            "(log has been rotated at %d-%02d-%02d %02d:%02d, "
                            "which is less than a day ago)\n", 1900 + state->lastRotated.tm_year,
                            1 + state->lastRotated.tm_mon, state->lastRotated.tm_mday,
                            state->lastRotated.tm_hour, state->lastRotated.tm_min);
                }
                break;
            case ROT_MONTHLY:
                /* rotate if the logs haven't been rotated this month or
                   this year */
                state->doRotate = ((now.tm_mon != state->lastRotated.tm_mon) ||
                        (now.tm_year != state->lastRotated.tm_year));
                if (!state->doRotate) {
                    message(MESS_DEBUG, "  log does not need rotating "
                            "(log has been rotated at %d-%02d-%02d %02d:%02d, "
                            "which is less than a month ago)\n", 1900 + state->lastRotated.tm_year,
                            1 + state->lastRotated.tm_mon, state->lastRotated.tm_mday,
                            state->lastRotated.tm_hour, state->lastRotated.tm_min);
                }
                break;
            case ROT_YEARLY:
                /* rotate if the logs haven't been rotated this year */
                state->doRotate = (now.tm_year != state->lastRotated.tm_year);
                if (!state->doRotate) {
                    message(MESS_DEBUG, "  log does not need rotating "
                            "(log has been rotated at %d-%02d-%02d %02d:%02d, "
                            "which is less than a year ago)\n", 1900 + state->lastRotated.tm_year,
                            1 + state->lastRotated.tm_mon, state->lastRotated.tm_mday,
                            state->lastRotated.tm_hour, state->lastRotated.tm_min);
                }
                break;
            case ROT_SIZE:
            default:
                /* ack! */
                state->doRotate = 0;
                break;
        }
        if (log->minsize && sb.st_size < log->minsize) {
            state->doRotate = 0;
            message(MESS_DEBUG, "  log does not need rotating "
                    "('minsize' directive is used and the log "
                    "size is smaller than the minsize value)\n");
        }
        if (log->rotateMinAge && log->rotateMinAge * DAY_SECONDS >= nowSecs - sb.st_mtime) {
            state->doRotate = 0;
            message(MESS_DEBUG, "  log does not need rotating "
                    "('minage' directive is used and the log "
                    "age is smaller than the minage days)\n");
        }
    }
    else if (!state->doRotate) {
        message(MESS_DEBUG, "  log does not need rotating "
                "(log has already been rotated)\n");
    }

    /* The notifempty flag overrides the normal criteria */
    if (state->doRotate && !(log->flags & LOG_FLAG_IFEMPTY) && !sb.st_size) {
        state->doRotate = 0;
        message(MESS_DEBUG, "  log does not need rotating "
                "(log is empty)\n");
    }

    if (state->doRotate) {
        message(MESS_DEBUG, "  log needs rotating\n");
    }

    return 0;
}

/* find the rotated file with the highest index */
static int findLastRotated(const struct logNames *rotNames,
                           const char *fileext, const char *compext)
{
    char *pattern;
    int glob_rc;
    glob_t globResult;
    size_t i;
    int last = 0;
    size_t prefixLen, suffixLen;

    if (asprintf(&pattern, "%s/%s.*%s%s", rotNames->dirName,
                 rotNames->baseName, fileext, compext) < 0)
        /* out of memory */
        return -1;

    glob_rc = glob(pattern, 0, globerr, &globResult);
    free(pattern);
    switch (glob_rc) {
        case 0:
            /* glob() succeeded */
            break;

        case GLOB_NOMATCH:
            /* found nothing -> assume first rotation */
            return 0;

        default:
            /* glob() failed */
            return -1;
    }

    prefixLen = strlen(rotNames->dirName) + /* '/' */1
        + strlen(rotNames->baseName) + /* '.' */ 1;
    suffixLen = strlen(fileext) + strlen(compext);

    for (i = 0; i < globResult.gl_pathc; ++i) {
        char *fileName = globResult.gl_pathv[i];
        const size_t fileNameLen = strlen(fileName);
        int num;
        char c;
        if (fileNameLen <= prefixLen + suffixLen)
            /* not enough room for index in this file name */
            continue;

        /* cut off prefix/suffix */
        fileName[fileNameLen - suffixLen] = '\0';
        fileName += prefixLen;

        if (sscanf(fileName, "%d%c", &num, &c) != 1)
            /* index not matched in this file name */
            continue;

        /* update last index */
        if (last < num)
            last = num;
    }

    globfree(&globResult);
    return last;
}

static int prerotateSingleLog(const struct logInfo *log, unsigned logNum,
                              struct logState *state, struct logNames *rotNames)
{
    struct tm now;
    const char *compext = "";
    const char *fileext = "";
    int hasErrors = 0;
    char *glob_pattern;
    glob_t globResult;
    int rc;
    int rotateCount = log->rotateCount ? log->rotateCount : 1;
    int logStart = (log->logStart == -1) ? 1 : log->logStart;
#define DATEEXT_LEN 64
#define PATTERN_LEN (DATEEXT_LEN * 2)
    char dext_str[DATEEXT_LEN];
    char dformat[PATTERN_LEN] = "";
    char dext_pattern[PATTERN_LEN];

    if (!state->doRotate)
        return 0;

    /* Logs with rotateCounts of 0 are rotated once, then removed. This
       lets scripts run properly, and everything gets mailed properly. */

    message(MESS_DEBUG, "rotating log %s, log->rotateCount is %d\n",
            log->files[logNum], log->rotateCount);

    if (log->flags & LOG_FLAG_COMPRESS) {
        if (!log->compress_ext) {
            message(MESS_ERROR, "log %s: compression enabled, but compression "
                "extension is not set\n", log->files[logNum]);
            return 1;
        }

        compext = log->compress_ext;
    }

    localtime_r(&nowSecs, &now);
    state->lastRotated = now;

    {
        const char *ld;
        char *logpath = strdup(log->files[logNum]);
        if (logpath == NULL) {
            message_OOM();
            return 1;
        }
        ld = dirname(logpath);
        if (log->oldDir) {
            if (log->oldDir[0] != '/') {
                if (asprintf(&rotNames->dirName, "%s/%s", ld, log->oldDir) < 0) {
                    rotNames->dirName = NULL;
                }
            } else
                rotNames->dirName = strdup(log->oldDir);
        } else
            rotNames->dirName = strdup(ld);
        free(logpath);

        if (rotNames->dirName == NULL) {
            message_OOM();
            return 1;
        }
    }

    {
        char *filename = strdup(log->files[logNum]);
        if (filename == NULL) {
            message_OOM();
            return 1;
        }

        rotNames->baseName = strdup(basename(filename));
        if (rotNames->baseName == NULL) {
            message_OOM();
            free(filename);
            return 1;
        }

        free(filename);
    }

    if (log->addextension) {
        const size_t baseLen = strlen(rotNames->baseName);
        const size_t extLen = strlen(log->addextension);
        if (baseLen >= extLen &&
                strncmp(&(rotNames->baseName[baseLen - extLen]),
                    log->addextension, extLen) == 0) {

            char *tempstr = strndup(rotNames->baseName, baseLen - extLen);
            if (tempstr == NULL) {
                message_OOM();
                return 1;
            }

            free(rotNames->baseName);
            rotNames->baseName = tempstr;
        }
        fileext = log->addextension;
    }

    if (log->extension) {
        const size_t baseLen = strlen(rotNames->baseName);
        const size_t extLen = strlen(log->extension);

        if (baseLen >= extLen &&
                strncmp(&(rotNames->baseName[baseLen - extLen]),
                    log->extension, extLen) == 0) {
            char *tempstr;

            fileext = log->extension;
            tempstr = strndup(rotNames->baseName, baseLen - extLen);
            if (tempstr == NULL) {
                message_OOM();
                return 1;
            }
            free(rotNames->baseName);
            rotNames->baseName = tempstr;
        }
    }

    /* Adjust "now" if we want yesterday's date */
    if (log->flags & LOG_FLAG_DATEYESTERDAY) {
        now.tm_hour = 12; /* set hour to noon to work around DST issues */
        now.tm_mday = now.tm_mday - 1;
        mktime(&now);
    }

    if (log->flags & LOG_FLAG_DATEHOURAGO) {
        now.tm_hour -= 1;
        mktime(&now);
    }

    /* Construct the glob pattern corresponding to the date format */
    dext_str[0] = '\0';
    if (log->dateformat) {
        char *dext;
        size_t i = 0, j = 0;
        memset(dext_pattern, 0, sizeof(dext_pattern));
        dext = log->dateformat;
        while (*dext == ' ')
            dext++;
        while ((*dext != '\0') && (!hasErrors)) {
            /* Will there be a space for a char and '\0'? */
            if (j >= (sizeof(dext_pattern) - 1)) {
                message(MESS_ERROR, "Date format %s is too long\n",
                        log->dateformat);
                hasErrors = 1;
                break;
            }
            if (*dext == '%') {
                switch (*(dext + 1)) {
                    case 'Y':
                        strncat(dext_pattern, "[0-9][0-9]",
                                sizeof(dext_pattern) - strlen(dext_pattern) - 1);
                        j += 10; /* strlen("[0-9][0-9]") */
                        /* FALLTHRU */
                    case 'm':
                    case 'd':
                    case 'H':
                    case 'M':
                    case 'S':
                    case 'V':
                        strncat(dext_pattern, "[0-9][0-9]",
                                sizeof(dext_pattern) - strlen(dext_pattern) - 1);
                        j += 10;
                        if (j >= (sizeof(dext_pattern) - 1)) {
                            message(MESS_ERROR, "Date format %s is too long\n",
                                    log->dateformat);
                            hasErrors = 1;
                            break;
                        }
                        dformat[i++] = *(dext++);
                        dformat[i] = *dext;
                        break;
                    case 's':
                        /* End of year 2293 this pattern does not work. */
                        strncat(dext_pattern,
                                "[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]",
                                sizeof(dext_pattern) - strlen(dext_pattern) - 1);
                        j += 50;
                        if (j >= (sizeof(dext_pattern) - 1)) {
                            message(MESS_ERROR, "Date format %s is too long\n",
                                    log->dateformat);
                            hasErrors = 1;
                            break;
                        }
                        dformat[i++] = *(dext++);
                        dformat[i] = *dext;
                        break;
                    default:
                        dformat[i++] = *dext;
                        dformat[i] = '%';
                        dext_pattern[j++] = *dext;
                        break;
                }
            } else {
                dformat[i] = *dext;
                dext_pattern[j++] = *dext;
            }
            ++i;
            ++dext;
        }
        dformat[i] = '\0';
        message(MESS_DEBUG, "Converted '%s' -> '%s'\n", log->dateformat, dformat);
        strftime(dext_str, sizeof(dext_str), dformat, &now);
    } else {
        if (log->criterium == ROT_HOURLY) {
            /* hourly adds another two digits */
            strftime(dext_str, sizeof(dext_str), "-%Y%m%d%H", &now);
            strncpy(dext_pattern, "-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]",
                    sizeof(dext_pattern));
        } else {
            /* The default dateformat and glob pattern */
            strftime(dext_str, sizeof(dext_str), "-%Y%m%d", &now);
            strncpy(dext_pattern, "-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]",
                    sizeof(dext_pattern));
        }
        dext_pattern[PATTERN_LEN - 1] = '\0';
    }
    message(MESS_DEBUG, "dateext suffix '%s'\n", dext_str);
    message(MESS_DEBUG, "glob pattern '%s'\n", dext_pattern);

    if (setSecCtxByName(log->files[logNum], &prev_context) != 0) {
        /* error msg already printed */
        return 1;
    }

    /* First compress the previous log when necessary */
    if ((log->flags & LOG_FLAG_COMPRESS) &&
            (log->flags & LOG_FLAG_DELAYCOMPRESS)) {
        if (log->flags & LOG_FLAG_DATEEXT) {
            /* glob for uncompressed files with our pattern */
            if (asprintf(&glob_pattern, "%s/%s%s%s", rotNames->dirName,
                         rotNames->baseName, dext_pattern, fileext) < 0) {
                message_OOM();
                return 1;
            }
            rc = glob(glob_pattern, 0, globerr, &globResult);
            if (!rc && globResult.gl_pathc > 0) {
                size_t glob_count;
                sortGlobResult(&globResult, strlen(rotNames->dirName) + 1 + strlen(rotNames->baseName), dformat);
                for (glob_count = 0; glob_count < globResult.gl_pathc && !hasErrors; glob_count++) {
                    struct stat sbprev;
                    const char *oldName = globResult.gl_pathv[glob_count];

                    if (stat(oldName, &sbprev)) {
                        if (errno == ENOENT)
                            message(MESS_DEBUG, "previous log %s does not exist\n", oldName);
                        else
                            message(MESS_ERROR, "cannot stat %s: %s\n", oldName, strerror(errno));
                    } else {
                        hasErrors = compressLogFile(oldName, log, &sbprev);
                    }
                }
            } else {
                message(MESS_DEBUG,
                        "glob finding logs to compress failed\n");
            }
            globfree(&globResult);
            free(glob_pattern);
        } else {
            struct stat sbprev;
            char *oldName;
            if (asprintf(&oldName, "%s/%s.%d%s", rotNames->dirName,
                         rotNames->baseName, logStart, fileext) < 0) {
                message_OOM();
                return 1;
            }
            if (stat(oldName, &sbprev)) {
                if (errno == ENOENT)
                    message(MESS_DEBUG, "previous log %s does not exist\n", oldName);
                else
                    message(MESS_ERROR, "cannot stat %s: %s\n", oldName, strerror(errno));
            } else {
                hasErrors = compressLogFile(oldName, log, &sbprev);
            }
            free(oldName);
        }
    }

    if (log->flags & LOG_FLAG_DATEEXT) {
        /* glob for compressed files with our pattern
         * and compress ext */
        if (asprintf(&glob_pattern, "%s/%s%s%s%s", rotNames->dirName,
                     rotNames->baseName, dext_pattern, fileext, compext) < 0) {
            message_OOM();
            return 1;
        }
        rc = glob(glob_pattern, 0, globerr, &globResult);
        if (!rc) {
            /* search for files to drop, if we find one remember it,
             * if we find another one mail and remove the first and
             * remember the second and so on */
            struct stat fst_buf;
            size_t glob_count, mail_out = (size_t)-1;
            /* Remove the first (n - rotateCount) matches no real rotation
             * needed, since the files have the date in their name. Note that
             * (size_t)-1 == SIZE_T_MAX in rotateCount */
            sortGlobResult(&globResult, strlen(rotNames->dirName) + 1 + strlen(rotNames->baseName), dformat);
            for (glob_count = 0; glob_count < globResult.gl_pathc; glob_count++) {
                if (!stat((globResult.gl_pathv)[glob_count], &fst_buf)) {
                    if (((globResult.gl_pathc >= (size_t)rotateCount) && (glob_count <= (globResult.gl_pathc - (size_t)rotateCount)))
                            || ((log->rotateAge > 0)
                                &&
                                (((nowSecs - fst_buf.st_mtime) / DAY_SECONDS)
                                 > log->rotateAge))) {
                        if (mail_out != (size_t)-1) {
                            char *mailFilename =
                                (globResult.gl_pathv)[mail_out];
                            if (!hasErrors && log->logAddress)
                                hasErrors = mailLogWrapper(mailFilename, mailCommand,
                                                           logNum, log);
                            if (!hasErrors) {
                                message(MESS_DEBUG, "removing %s\n", mailFilename);
                                hasErrors = removeLogFile(mailFilename, log);
                            }
                        }
                        mail_out = glob_count;
                    }
                }
            }
            if (mail_out != (size_t)-1) {
                /* oldName is oldest Backup found (for unlink later) */
                const char *oldName = globResult.gl_pathv[mail_out];
                rotNames->disposeName = strdup(oldName);
                if (rotNames->disposeName == NULL) {
                    message_OOM();
                    globfree(&globResult);
                    free(glob_pattern);
                    return 1;
                }
            } else {
                free(rotNames->disposeName);
                rotNames->disposeName = NULL;
            }
        } else {
            message(MESS_DEBUG, "glob finding old rotated logs failed\n");
            free(rotNames->disposeName);
            rotNames->disposeName = NULL;
        }
        /* firstRotated is most recently created/compressed rotated log */
        if (asprintf(&rotNames->firstRotated, "%s/%s%s%s%s",
                rotNames->dirName, rotNames->baseName, dext_str, fileext,
                (log->flags & LOG_FLAG_DELAYCOMPRESS) ? "" : compext) < 0) {
            message_OOM();
            rotNames->firstRotated = NULL;
            globfree(&globResult);
            free(glob_pattern);
            return 1;
        }
        globfree(&globResult);
        free(glob_pattern);
    } else {
        int i;
        char *newName = NULL;
        char *oldName;

        if (rotateCount == -1) {
            rotateCount = findLastRotated(rotNames, fileext, compext);
            if (rotateCount < 0) {
                message(MESS_ERROR, "could not find last rotated file: %s/%s.*%s%s\n",
                        rotNames->dirName, rotNames->baseName, fileext, compext);
                return 1;
            }
        }

        if (asprintf(&oldName, "%s/%s.%d%s%s", rotNames->dirName,
                     rotNames->baseName, logStart + rotateCount, fileext,
                     compext) < 0) {
            message_OOM();
            return 1;
        }

        if (log->rotateCount != -1) {
            rotNames->disposeName = strdup(oldName);
            if (rotNames->disposeName == NULL) {
                message_OOM();
                free(oldName);
                return 1;
            }
        }

        if (asprintf(&rotNames->firstRotated, "%s/%s.%d%s%s", rotNames->dirName,
                rotNames->baseName, logStart, fileext,
                (log->flags & LOG_FLAG_DELAYCOMPRESS) ? "" : compext) < 0) {
            message_OOM();
            free(oldName);
            rotNames->firstRotated = NULL;
            return 1;
        }

        for (i = rotateCount + logStart - 1; (i >= 0) && !hasErrors; i--) {
            free(newName);
            newName = oldName;
            if (asprintf(&oldName, "%s/%s.%d%s%s", rotNames->dirName,
                         rotNames->baseName, i, fileext, compext) < 0) {
                message_OOM();
                oldName = NULL;
                break;
            }

            /* remove files hit by maxage */
            if (log->rotateAge) {
                struct stat fst_buf;

                if (stat(oldName, &fst_buf)) {
                    if (errno == ENOENT) {
                        message(MESS_DEBUG, "old log %s does not exist\n",
                                oldName);
                    } else {
                        message(MESS_ERROR, "cannot stat %s: %s\n", oldName,
                                strerror(errno));
                        hasErrors = 1;
                    }

                    continue;
                }

                if (((nowSecs - fst_buf.st_mtime) / DAY_SECONDS) > log->rotateAge) {
                    if (!hasErrors && log->logAddress)
                        hasErrors = mailLogWrapper(oldName, mailCommand,
                                                   logNum, log);
                    if (!hasErrors)
                        hasErrors = removeLogFile(oldName, log);

                    continue;
                }
            }

            message(MESS_DEBUG,
                    "renaming %s to %s (rotatecount %d, logstart %d, i %d), \n",
                    oldName, newName, rotateCount, logStart, i);

            if (!debug && rename(oldName, newName)) {
                if (errno == ENOENT) {
                    message(MESS_DEBUG, "old log %s does not exist\n",
                            oldName);
                } else {
                    message(MESS_ERROR, "error renaming %s to %s: %s\n",
                            oldName, newName, strerror(errno));
                    hasErrors = 1;
                }
            }
        }
        free(newName);
        free(oldName);
    } /* !LOG_FLAG_DATEEXT */

    if (log->flags & LOG_FLAG_DATEEXT) {
        char *destFile;
        struct stat fst_buf;

        if (asprintf(&(rotNames->finalName), "%s/%s%s%s", rotNames->dirName,
                     rotNames->baseName, dext_str, fileext) < 0) {
            message_OOM();
            rotNames->finalName = NULL;
            return 1;
        }
        if (asprintf(&destFile, "%s%s", rotNames->finalName, compext) < 0) {
            message_OOM();
            return 1;
        }
        if (!stat(destFile, &fst_buf)) {
            message(MESS_ERROR,
                    "destination %s already exists, skipping rotation\n",
                    rotNames->firstRotated);
            hasErrors = 1;
        }
        free(destFile);
    } else {
        /* note: the gzip extension is *not* used here! */
        if (asprintf(&(rotNames->finalName), "%s/%s.%d%s", rotNames->dirName,
                     rotNames->baseName, logStart, fileext) < 0) {
            message_OOM();
            rotNames->finalName = NULL;
        }
    }

    /* if the last rotation doesn't exist, that's okay */
    if (rotNames->disposeName && access(rotNames->disposeName, F_OK)) {
        message(MESS_DEBUG,
                "log %s doesn't exist -- won't try to dispose of it\n",
                rotNames->disposeName);
        free(rotNames->disposeName);
        rotNames->disposeName = NULL;
    }

    return hasErrors;
}

static int rotateSingleLog(const struct logInfo *log, unsigned logNum,
                           struct logState *state, struct logNames *rotNames)
{
    int hasErrors = 0;
    struct stat sb;
    char *savedContext = NULL;

    if (!state->doRotate)
        return 0;

    if (!hasErrors) {

        if (!(log->flags & (LOG_FLAG_COPYTRUNCATE | LOG_FLAG_COPY))) {
            if (setSecCtxByName(log->files[logNum], &savedContext) != 0) {
                /* error msg already printed */
                return 1;
            }
#ifdef WITH_ACL
            if ((prev_acl = acl_get_file(log->files[logNum], ACL_TYPE_ACCESS)) == NULL) {
                if (is_acl_well_supported(errno)) {
                    message(MESS_ERROR, "getting file ACL %s: %s\n",
                            log->files[logNum], strerror(errno));
                    hasErrors = 1;
                }
            }
#endif /* WITH_ACL */
            if (log->flags & LOG_FLAG_TMPFILENAME) {
                char *tmpFilename;

                if (asprintf(&tmpFilename, "%s%s", log->files[logNum], ".tmp") < 0) {
                    message_OOM();
                    restoreSecCtx(&savedContext);
                    return 1;
                }

                message(MESS_DEBUG, "renaming %s to %s\n", log->files[logNum],
                        tmpFilename);
                if (!debug && !hasErrors && rename(log->files[logNum], tmpFilename)) {
                    message(MESS_ERROR, "failed to rename %s to %s: %s\n",
                            log->files[logNum], tmpFilename,
                            strerror(errno));
                    hasErrors = 1;
                }

                free(tmpFilename);
            }
            else {
                message(MESS_DEBUG, "renaming %s to %s\n", log->files[logNum],
                        rotNames->finalName);
                if (!debug && !hasErrors &&
                        rename(log->files[logNum], rotNames->finalName)) {
                    message(MESS_ERROR, "failed to rename %s to %s: %s\n",
                            log->files[logNum], rotNames->finalName,
                            strerror(errno));
                    hasErrors = 1;
                }
            }

            if (!log->rotateCount) {
                const char *ext = "";
                if (log->compress_ext
                        && (log->flags & LOG_FLAG_COMPRESS)
                        && !(log->flags & LOG_FLAG_DELAYCOMPRESS))
                    ext = log->compress_ext;

                free(rotNames->disposeName);
                if (asprintf(&rotNames->disposeName, "%s%s", rotNames->finalName, ext) < 0) {
                    message_OOM();
                    rotNames->disposeName = NULL;
                    return 1;
                }

                message(MESS_DEBUG, "disposeName will be %s\n", rotNames->disposeName);
            }
        }

        if (!hasErrors && (log->flags & LOG_FLAG_CREATE) &&
                !(log->flags & (LOG_FLAG_COPYTRUNCATE | LOG_FLAG_COPY))) {
            int have_create_mode = 0;

            if (log->createUid == NO_UID)
                sb.st_uid = state->sb.st_uid;
            else
                sb.st_uid = log->createUid;

            if (log->createGid == NO_GID)
                sb.st_gid = state->sb.st_gid;
            else
                sb.st_gid = log->createGid;
            if (log->createMode == NO_MODE)
                sb.st_mode = state->sb.st_mode & 0777;
            else {
                sb.st_mode = log->createMode;
                have_create_mode = 1;
            }

            message(MESS_DEBUG, "creating new %s mode = 0%o uid = %d "
                    "gid = %d\n", log->files[logNum], (unsigned int) sb.st_mode,
                    (int) sb.st_uid, (int) sb.st_gid);

            if (!debug) {
                if (!hasErrors) {
                    int fd = createOutputFile(log->files[logNum], O_CREAT | O_RDWR,
                            &sb, prev_acl, have_create_mode);
#ifdef WITH_ACL
                    if (prev_acl) {
                        acl_free(prev_acl);
                        prev_acl = NULL;
                    }
#endif
                    if (fd < 0)
                        hasErrors = 1;
                    else {
                        close(fd);
                    }
                }
            }
        }

        restoreSecCtx(&savedContext);

        if (!hasErrors
                && (log->flags & (LOG_FLAG_COPYTRUNCATE | LOG_FLAG_COPY))
                && !(log->flags & LOG_FLAG_TMPFILENAME)) {
            hasErrors = copyTruncate(log->files[logNum], rotNames->finalName,
                                     &state->sb, log->flags, !log->rotateCount);
        }

#ifdef WITH_ACL
        if (prev_acl) {
            acl_free(prev_acl);
            prev_acl = NULL;
        }
#endif /* WITH_ACL */

    }
    return hasErrors;
}

static int postrotateSingleLog(const struct logInfo *log, unsigned logNum,
                               struct logState *state,
                               struct logNames *rotNames)
{
    int hasErrors = 0;

    if (!state->doRotate) {
        return 0;
    }

    if (!hasErrors && (log->flags & LOG_FLAG_TMPFILENAME)) {
        char *tmpFilename;
        if (asprintf(&tmpFilename, "%s%s", log->files[logNum], ".tmp") < 0) {
            message_OOM();
            return 1;
        }
        hasErrors = copyTruncate(tmpFilename, rotNames->finalName,
                                 &state->sb, LOG_FLAG_COPY, /* skip_copy */ 0);
        message(MESS_DEBUG, "removing tmp log %s\n", tmpFilename);
        if (!debug && !hasErrors) {
            unlink(tmpFilename);
        }
        free(tmpFilename);
    }

    if (!hasErrors && (log->flags & LOG_FLAG_COMPRESS) &&
            !(log->flags & LOG_FLAG_DELAYCOMPRESS)) {
        hasErrors = compressLogFile(rotNames->finalName, log, &state->sb);
    }

    if (!hasErrors && log->logAddress) {
        char *mailFilename;

        if (log->flags & LOG_FLAG_MAILFIRST)
            mailFilename = rotNames->firstRotated;
        else
            mailFilename = rotNames->disposeName;

        if (mailFilename)
            hasErrors = mailLogWrapper(mailFilename, mailCommand, logNum, log);
    }

    if (!hasErrors && rotNames->disposeName)
        hasErrors = removeLogFile(rotNames->disposeName, log);

    restoreSecCtx(&prev_context);
    return hasErrors;
}

static int rotateLogSet(const struct logInfo *log, int force)
{
    unsigned i, j;
    int hasErrors = 0;
    int *logHasErrors;
    int numRotated = 0;
    struct logState **state;
    struct logNames **rotNames;

    message(MESS_DEBUG, "\nrotating pattern: %s ", log->pattern);
    if (force) {
        message(MESS_DEBUG, "forced from command line ");
    }
    else {
        switch (log->criterium) {
            case ROT_HOURLY:
                message(MESS_DEBUG, "hourly ");
                break;
            case ROT_DAYS:
                message(MESS_DEBUG, "after %jd days ", (intmax_t)log->threshold);
                break;
            case ROT_WEEKLY:
                message(MESS_DEBUG, "weekly ");
                break;
            case ROT_MONTHLY:
                message(MESS_DEBUG, "monthly ");
                break;
            case ROT_YEARLY:
                message(MESS_DEBUG, "yearly ");
                break;
            case ROT_SIZE:
                message(MESS_DEBUG, "%jd bytes ", (intmax_t)log->threshold);
                break;
            default:
                message(MESS_FATAL, "rotateLogSet() does not have case for: %u ",
                        (unsigned) log->criterium);
        }
    }

    if (log->rotateCount > 0)
        message(MESS_DEBUG, "(%d rotations)\n", log->rotateCount);
    else if (log->rotateCount == 0)
        message(MESS_DEBUG, "(no old logs will be kept)\n");

    if (log->oldDir)
        message(MESS_DEBUG, "olddir is %s, ", log->oldDir);

    if (log->flags & LOG_FLAG_IFEMPTY)
        message(MESS_DEBUG, "empty log files are rotated, ");
    else
        message(MESS_DEBUG, "empty log files are not rotated, ");

    if (log->minsize)
        message(MESS_DEBUG, "only log files >= %jd bytes are rotated, ", (intmax_t)log->minsize);

    if (log->maxsize)
        message(MESS_DEBUG, "log files >= %jd are rotated earlier, ", (intmax_t)log->maxsize);

    if (log->rotateMinAge)
        message(MESS_DEBUG, "only log files older than %d days are rotated, ", log->rotateMinAge);

    if (log->logAddress) {
        message(MESS_DEBUG, "old logs mailed to %s\n", log->logAddress);
    } else {
        message(MESS_DEBUG, "old logs are removed\n");
    }

    if (log->numFiles == 0) {
        message(MESS_DEBUG, "No logs found. Rotation not needed.\n");
        return 0;
    }

    logHasErrors = calloc(log->numFiles, sizeof(int));
    if (!logHasErrors) {
        message_OOM();
        return 1;
    }

    if (log->flags & LOG_FLAG_SU) {
        if (switch_user(log->suUid, log->suGid) != 0) {
            free(logHasErrors);
            return 1;
        }
    }

    for (i = 0; i < log->numFiles; i++) {
        struct logState *logState;
        logHasErrors[i] = findNeedRotating(log, i, force);
        hasErrors |= logHasErrors[i];

        /* sure is a lot of findStating going on .. */
        if (((logState = findState(log->files[i]))) && logState->doRotate)
            numRotated++;
    }

    if (log->first) {
        if (!numRotated) {
            message(MESS_DEBUG, "not running first action script, "
                    "since no logs will be rotated\n");
        } else {
            message(MESS_DEBUG, "running first action script\n");
            if (runScript(log, log->pattern, NULL, log->first)) {
                message(MESS_ERROR, "error running first action script "
                        "for %s\n", log->pattern);
                hasErrors = 1;
                if (log->flags & LOG_FLAG_SU) {
                    if (switch_user_back() != 0) {
                        free(logHasErrors);
                        return 1;
                    }
                }
                /* finish early, firstaction failed, affects all logs in set */
                free(logHasErrors);
                return hasErrors;
            }
        }
    }

    state = malloc(log->numFiles * sizeof(struct logState *));
    rotNames = malloc(log->numFiles * sizeof(struct logNames *));

    if (state == NULL || rotNames == NULL) {
        message_OOM();
        if (log->flags & LOG_FLAG_SU) {
            switch_user_back();
        }
        free(rotNames);
        free(state);
        free(logHasErrors);
        return 1;
    }

    for (j = 0;
            (!(log->flags & LOG_FLAG_SHAREDSCRIPTS) && j < log->numFiles)
            || ((log->flags & LOG_FLAG_SHAREDSCRIPTS) && j < 1); j++) {

        for (i = j;
                ((log->flags & LOG_FLAG_SHAREDSCRIPTS) && i < log->numFiles)
                || (!(log->flags & LOG_FLAG_SHAREDSCRIPTS) && i == j); i++) {
            state[i] = findState(log->files[i]);
            if (!state[i])
                logHasErrors[i] = 1;

            rotNames[i] = malloc(sizeof(struct logNames));
            if (rotNames[i] == NULL) {
                message_OOM();
                if (log->flags & LOG_FLAG_SU) {
                    switch_user_back();
                }
                free(rotNames);
                free(state);
                free(logHasErrors);
                return 1;
            }
            memset(rotNames[i], 0, sizeof(struct logNames));

            logHasErrors[i] |=
                prerotateSingleLog(log, i, state[i], rotNames[i]);
            hasErrors |= logHasErrors[i];
        }

        if (log->pre
                && (!(
                        (!(log->flags & LOG_FLAG_SHAREDSCRIPTS) && (logHasErrors[j] || !state[j]->doRotate))
                        || (hasErrors && (log->flags & LOG_FLAG_SHAREDSCRIPTS))
                     ))
           ) {
            if (!numRotated) {
                message(MESS_DEBUG, "not running prerotate script, "
                        "since no logs will be rotated\n");
            } else {
                message(MESS_DEBUG, "running prerotate script\n");
                if (runScript(log, (log->flags & LOG_FLAG_SHAREDSCRIPTS) ? log->pattern : log->files[j], NULL, log->pre)) {
                    if (log->flags & LOG_FLAG_SHAREDSCRIPTS)
                        message(MESS_ERROR,
                                "error running shared prerotate script "
                                "for '%s'\n", log->pattern);
                    else {
                        message(MESS_ERROR,
                                "error running non-shared prerotate script "
                                "for %s of '%s'\n", log->files[j], log->pattern);
                    }
                    logHasErrors[j] = 1;
                    hasErrors = 1;
                }
            }
        }

        for (i = j;
                ((log->flags & LOG_FLAG_SHAREDSCRIPTS) && i < log->numFiles)
                || (!(log->flags & LOG_FLAG_SHAREDSCRIPTS) && i == j); i++) {
            if (! ( (logHasErrors[i] && !(log->flags & LOG_FLAG_SHAREDSCRIPTS))
                        || (hasErrors && (log->flags & LOG_FLAG_SHAREDSCRIPTS)) ) ) {
                logHasErrors[i] |=
                    rotateSingleLog(log, i, state[i], rotNames[i]);
                hasErrors |= logHasErrors[i];
            }
        }

        if (log->post
                && (!(
                        (!(log->flags & LOG_FLAG_SHAREDSCRIPTS) && (logHasErrors[j] || !state[j]->doRotate))
                        || (hasErrors && (log->flags & LOG_FLAG_SHAREDSCRIPTS))
                     ))
           ) {
            if (!numRotated) {
                message(MESS_DEBUG, "not running postrotate script, "
                        "since no logs were rotated\n");
            } else {
                char *logfn = (log->flags & LOG_FLAG_SHAREDSCRIPTS) ? log->pattern : log->files[j];

                /* It only makes sense to pass in a final rotated filename if scripts are not shared */
                char *logrotfn = (log->flags & LOG_FLAG_SHAREDSCRIPTS) ? NULL : rotNames[j]->finalName;

                message(MESS_DEBUG, "running postrotate script\n");
                if (runScript(log, logfn, logrotfn, log->post)) {
                    if (log->flags & LOG_FLAG_SHAREDSCRIPTS)
                        message(MESS_ERROR,
                                "error running shared postrotate script "
                                "for '%s'\n", log->pattern);
                    else {
                        message(MESS_ERROR,
                                "error running non-shared postrotate script "
                                "for %s of '%s'\n", log->files[j], log->pattern);
                    }
                    logHasErrors[j] = 1;
                    hasErrors = 1;
                }
            }
        }

        for (i = j;
                ((log->flags & LOG_FLAG_SHAREDSCRIPTS) && i < log->numFiles)
                || (!(log->flags & LOG_FLAG_SHAREDSCRIPTS) && i == j); i++) {
            if (! ( (logHasErrors[i] && !(log->flags & LOG_FLAG_SHAREDSCRIPTS))
                        || (hasErrors && (log->flags & LOG_FLAG_SHAREDSCRIPTS)) ) ) {
                logHasErrors[i] |=
                    postrotateSingleLog(log, i, state[i], rotNames[i]);
                hasErrors |= logHasErrors[i];
            }
        }

    }

    for (i = 0; i < log->numFiles; i++) {
        free(rotNames[i]->firstRotated);
        free(rotNames[i]->disposeName);
        free(rotNames[i]->finalName);
        free(rotNames[i]->dirName);
        free(rotNames[i]->baseName);
        free(rotNames[i]);
    }
    free(rotNames);
    free(state);

    if (log->last) {
        if (!numRotated) {
            message(MESS_DEBUG, "not running last action script, "
                    "since no logs will be rotated\n");
        } else {
            message(MESS_DEBUG, "running last action script\n");
            if (runScript(log, log->pattern, NULL, log->last)) {
                message(MESS_ERROR, "error running last action script "
                        "for %s\n", log->pattern);
                hasErrors = 1;
            }
        }
    }

    if (log->flags & LOG_FLAG_SU) {
        if (switch_user_back() != 0) {
            free(logHasErrors);
            return 1;
        }
    }
    free(logHasErrors);
    return hasErrors;
}

static int writeState(const char *stateFilename)
{
    struct logState *p;
    FILE *f;
    char *chptr;
    unsigned int i = 0;
    int error = 0;
    int bytes = 0;
    int fdcurr;
    int fdsave;
    struct stat sb;
    char *tmpFilename = NULL;
    struct tm now;
    time_t now_time, last_time;
    char *prevCtx;

    localtime_r(&nowSecs, &now);

    tmpFilename = malloc(strlen(stateFilename) + 5 );
    if (tmpFilename == NULL) {
        message_OOM();
        return 1;
    }
    strcpy(tmpFilename, stateFilename);
    strcat(tmpFilename, ".tmp");
    /* Remove possible tmp state file from previous run */
    error = unlink(tmpFilename);
    if (error == -1 && errno != ENOENT) {
        message(MESS_ERROR, "error removing old temporary state file %s: %s\n",
                tmpFilename, strerror(errno));
        free(tmpFilename);
        return 1;
    }
    error = 0;

    fdcurr = open(stateFilename, O_RDONLY);
    if (fdcurr == -1) {
        /* the statefile should exist, lockState() already created an empty
         * state file in case it did not exist initially */
        message(MESS_ERROR, "error opening state file %s: %s\n",
                stateFilename, strerror(errno));
        free(tmpFilename);
        return 1;
    }

    /* get attributes, to assign them to the new state file */

    if (setSecCtx(fdcurr, stateFilename, &prevCtx) != 0) {
        /* error msg already printed */
        free(tmpFilename);
        close(fdcurr);
        return 1;
    }

#ifdef WITH_ACL
    if ((prev_acl = acl_get_fd(fdcurr)) == NULL) {
        if (is_acl_well_supported(errno)) {
            message(MESS_ERROR, "getting file ACL %s: %s\n",
                    stateFilename, strerror(errno));
            restoreSecCtx(&prevCtx);
            free(tmpFilename);
            close(fdcurr);
            return 1;
        }
    }
#endif

    if (fstat(fdcurr, &sb) == -1) {
        message(MESS_ERROR, "error stating %s: %s\n", stateFilename, strerror(errno));
        restoreSecCtx(&prevCtx);
        free(tmpFilename);
#ifdef WITH_ACL
        if (prev_acl) {
            acl_free(prev_acl);
            prev_acl = NULL;
        }
#endif
        return 1;
    }

    close(fdcurr);

    fdsave = createOutputFile(tmpFilename, O_RDWR | O_CREAT | O_TRUNC, &sb, prev_acl, 0);
#ifdef WITH_ACL
    if (prev_acl) {
        acl_free(prev_acl);
        prev_acl = NULL;
    }
#endif
    restoreSecCtx(&prevCtx);

    if (fdsave < 0) {
        free(tmpFilename);
        return 1;
    }

    f = fdopen(fdsave, "w");
    if (!f) {
        message(MESS_ERROR, "error creating temp state file %s: %s\n",
                tmpFilename, strerror(errno));
        free(tmpFilename);
        return 1;
    }

    bytes =  fprintf(f, "logrotate state -- version 2\n");
    if (bytes < 0)
        error = bytes;

    /*
     * Time in seconds it takes earth to go around sun.  The value is
     * astronomical measurement (solar year) rather than something derived from
     * a convention (calendar year).
     */
#define SECONDS_IN_YEAR 31556926

    for (i = 0; i < hashSize && error == 0; i++) {
        for (p = states[i]->head.lh_first; p != NULL && error == 0;
                p = p->list.le_next) {

            /* Skip states which are not used for more than a year. */
            now_time = mktime(&now);
            last_time = mktime(&p->lastRotated);
            if (!p->isUsed && difftime(now_time, last_time) > SECONDS_IN_YEAR) {
                message(MESS_DEBUG, "Removing %s from state file, "
                        "because it does not exist and has not been rotated for one year\n",
                        p->fn);
                continue;
            }

            error = fputc('"', f) == EOF;
            for (chptr = p->fn; *chptr && error == 0; chptr++) {
                switch (*chptr) {
                    case '"':
                    case '\\':
                        error = fputc('\\', f) == EOF;
                        break;
                    case '\n':
                        error = fputc('\\', f) == EOF;
                        if (error == 0) {
                            error = fputc('n', f) == EOF;
                        }
                        continue;
                    default:
                        break;
                }
                if (error == 0 && fputc(*chptr, f) == EOF) {
                    error = 1;
                }
            }

            if (error == 0 && fputc('"', f) == EOF)
                error = 1;

            if (error == 0) {
                bytes = fprintf(f, " %d-%d-%d-%d:%d:%d\n",
                                p->lastRotated.tm_year + 1900,
                                p->lastRotated.tm_mon + 1,
                                p->lastRotated.tm_mday,
                                p->lastRotated.tm_hour,
                                p->lastRotated.tm_min,
                                p->lastRotated.tm_sec);
                if (bytes < 0)
                    error = bytes;
            }
        }
    }

    if (error == 0)
        error = fflush(f);

    if (error == 0)
        error = fsync(fdsave);

    if (error == 0)
        error = fclose(f);
    else
        fclose(f);

    if (error == 0) {
        if (rename(tmpFilename, stateFilename)) {
            message(MESS_ERROR, "error renaming temp state file %s to %s: %s\n",
                    tmpFilename, stateFilename, strerror(errno));
            unlink(tmpFilename);
            error = 1;
        }
    }
    else {
        if (errno)
            message(MESS_ERROR, "error creating temp state file %s: %s\n",
                    tmpFilename, strerror(errno));
        else
            message(MESS_ERROR, "error creating temp state file %s%s\n",
                    tmpFilename, error == ENOMEM ?
                    ": Insufficient storage space is available." : "" );
        unlink(tmpFilename);
    }
    free(tmpFilename);
    return error;
}

static int readState(const char *stateFilename)
{
    FILE *f;
    char buf[STATEFILE_BUFFER_SIZE];
    char *filename;
    const char **argv;
    int argc;
    int year, month, day, hour, minute, second;
    int line = 0;
    int fd;
    struct logState *st;
    time_t lr_time;
    struct stat f_stat;
    int rc = 0;

    message(MESS_DEBUG, "Reading state from file: %s\n", stateFilename);

    fd = open(stateFilename, O_RDONLY);
    if (fd == -1) {
        /* treat non-openable file as an empty file for allocateHash() */
        f_stat.st_size = 0;

        /* the statefile should exist, lockState() already created an empty
         * state file in case it did not exist initially */
        message(MESS_ERROR, "error opening state file %s: %s\n",
                stateFilename, strerror(errno));

        /* Do not return until the hash table is allocated.
         * In debug mode the state file might not exist,
         * cause lockState() is not called */
        if (!debug) {
            rc = 1;
        }
    } else {
        if (fstat(fd, &f_stat) == -1) {
            /* treat non-statable file as an empty file for allocateHash() */
            f_stat.st_size = 0;

            message(MESS_ERROR, "error stat()ing state file %s: %s\n",
                    stateFilename, strerror(errno));

            /* do not return until the hash table is allocated */
            rc = 1;
        }
    }

    /* Try to estimate how many state entries we have in the state file.
     * We expect single entry to have around 80 characters (Of course this is
     * just an estimation). During the testing I've found out that 200 entries
     * per single hash entry gives good mem/performance ratio. */
    if (allocateHash((size_t)f_stat.st_size / 80 / 200))
        rc = 1;

    if (rc || (f_stat.st_size == 0)) {
        /* error already occurred, or we have no state file to read from */
        if (fd != -1)
            close(fd);
        return rc;
    }

    f = fdopen(fd, "r");
    if (!f) {
        message(MESS_ERROR, "error opening state file %s: %s\n",
                stateFilename, strerror(errno));
        close(fd);
        return 1;
    }

    if (!fgets(buf, sizeof(buf) - 1, f)) {
        message(MESS_ERROR, "error reading top line of %s\n",
                stateFilename);
        fclose(f);
        return 1;
    }

    if (strcmp(buf, "logrotate state -- version 1\n") != 0 &&
            strcmp(buf, "logrotate state -- version 2\n") != 0) {
        fclose(f);
        message(MESS_ERROR, "bad top line in state file %s\n",
                stateFilename);
        return 1;
    }

    line++;

    while (fgets(buf, sizeof(buf) - 1, f)) {
        const size_t i = strlen(buf);
        argv = NULL;
        line++;
        if (i == 0) {
            message(MESS_ERROR, "line %d not parsable in state file %s\n",
                    line, stateFilename);
            fclose(f);
            return 1;
        }
        if (buf[i - 1] != '\n') {
            message(MESS_ERROR, "line %d too long in state file %s\n",
                    line, stateFilename);
            fclose(f);
            return 1;
        }

        buf[i - 1] = '\0';

        if (i == 1)
            continue;

        year = month = day = hour = minute = second = 0;
        if (poptParseArgvString(buf, &argc, &argv) || (argc != 2) ||
                (sscanf(argv[1], "%d-%d-%d-%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 3)) {
            message(MESS_ERROR, "bad line %d in state file %s\n",
                    line, stateFilename);
            free(argv);
            fclose(f);
            return 1;
        }

        /* Hack to hide earlier bug */
        if ((year != 1900) && (year < 1970 || year > 2100)) {
            message(MESS_ERROR,
                    "bad year %d for file %s in state file %s\n", year,
                    argv[0], stateFilename);
            free(argv);
            fclose(f);
            return 1;
        }

        if (month < 1 || month > 12) {
            message(MESS_ERROR,
                    "bad month %d for file %s in state file %s\n", month,
                    argv[0], stateFilename);
            free(argv);
            fclose(f);
            return 1;
        }

        /* 0 to hide earlier bug */
        if (day < 0 || day > 31) {
            message(MESS_ERROR,
                    "bad day %d for file %s in state file %s\n", day,
                    argv[0], stateFilename);
            free(argv);
            fclose(f);
            return 1;
        }

        if (hour < 0 || hour > 23) {
            message(MESS_ERROR,
                    "bad hour %d for file %s in state file %s\n", hour,
                    argv[0], stateFilename);
            free(argv);
            fclose(f);
            return 1;
        }

        if (minute < 0 || minute > 59) {
            message(MESS_ERROR,
                    "bad minute %d for file %s in state file %s\n", minute,
                    argv[0], stateFilename);
            free(argv);
            fclose(f);
            return 1;
        }

        if (second < 0 || second > 59) {
            message(MESS_ERROR,
                    "bad second %d for file %s in state file %s\n", second,
                    argv[0], stateFilename);
            free(argv);
            fclose(f);
            return 1;
        }

        year -= 1900;
        month -= 1;

        filename = strdup(argv[0]);
        if (filename == NULL) {
            message_OOM();
            free(argv);
            fclose(f);
            return 1;
        }
        unescape(filename);

        if ((st = findState(filename)) == NULL) {
            free(argv);
            free(filename);
            fclose(f);
            return 1;
        }

        memset(&st->lastRotated, 0, sizeof(st->lastRotated));
        st->lastRotated.tm_year = year;
        st->lastRotated.tm_mon = month;
        st->lastRotated.tm_mday = day;
        st->lastRotated.tm_hour = hour;
        st->lastRotated.tm_min = minute;
        st->lastRotated.tm_sec = second;
        st->lastRotated.tm_isdst = -1;

        /* fill in the rest of the st->lastRotated fields */
        lr_time = mktime(&st->lastRotated);
        localtime_r(&lr_time, &st->lastRotated);

        free(argv);
        free(filename);
    }

    fclose(f);
    return 0;
}

static int lockState(const char *stateFilename, int skip_state_lock)
{
    int lockFd = open(stateFilename, O_RDWR | O_CLOEXEC);
    if (lockFd == -1) {
        if (errno == ENOENT) {
            message(MESS_DEBUG, "Creating stub state file: %s\n",
                    stateFilename);

            /* create a stub state file with mode 0644 */
            lockFd = open(stateFilename, O_CREAT | O_EXCL | O_WRONLY,
                          S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
            if (lockFd == -1) {
                message(MESS_ERROR, "error creating stub state file %s: %s\n",
                        stateFilename, strerror(errno));
                return 1;
            }
        } else {
            message(MESS_ERROR, "error opening state file %s: %s\n",
                    stateFilename, strerror(errno));
            return 1;
        }
    }

    if (skip_state_lock) {
        message(MESS_DEBUG, "Skip locking state file %s\n",
                stateFilename);
        close(lockFd);
        return 0;
    }

    if (flock(lockFd, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK) {
            message(MESS_ERROR, "state file %s is already locked\n"
                    "logrotate does not support parallel execution on the"
                    " same set of logfiles.\n", stateFilename);
        } else {
            message(MESS_ERROR, "error acquiring lock on state file %s: %s\n",
                    stateFilename, strerror(errno));
        }
        close(lockFd);
        return 1;
    }

    /* keep lockFd open till we terminate */
    return 0;
}

int main(int argc, const char **argv)
{
    int force = 0;
    int skip_state_lock = 0;
    const char *stateFile = STATEFILE;
    const char *logFile = NULL;
    FILE *logFd = NULL;
    int rc = 0;
    int arg;
    const char **files;
    poptContext optCon;
    struct logInfo *log;

    struct poptOption options[] = {
        {"debug", 'd', 0, NULL, 'd',
            "Don't do anything, just test and print debug messages", NULL},
        {"force", 'f', 0, &force, 0, "Force file rotation", NULL},
        {"mail", 'm', POPT_ARG_STRING, &mailCommand, 0,
            "Command to send mail (instead of `" DEFAULT_MAIL_COMMAND "')",
            "command"},
        {"state", 's', POPT_ARG_STRING, &stateFile, 0,
            "Path of state file",
            "statefile"},
        {"skip-state-lock", '\0', POPT_ARG_NONE, &skip_state_lock, 0, "Do not lock the state file", NULL},
        {"verbose", 'v', 0, NULL, 'v', "Display messages during rotation", NULL},
        {"log", 'l', POPT_ARG_STRING, &logFile, 'l', "Log file or 'syslog' to log to syslog",
            "logfile"},
        {"version", '\0', POPT_ARG_NONE, NULL, 'V', "Display version information", NULL},
        POPT_AUTOHELP { NULL, 0, 0, NULL, 0, NULL, NULL }
    };

    logSetLevel(MESS_NORMAL);
    setlocale (LC_ALL, "");

    optCon = poptGetContext("logrotate", argc, argv, options, 0);
    poptReadDefaultConfig(optCon, 1);
    poptSetOtherOptionHelp(optCon, "[OPTION...] <configfile>");

    while ((arg = poptGetNextOpt(optCon)) >= 0) {
        switch (arg) {
            case 'd':
                debug = 1;
                message(MESS_NORMAL, "WARNING: logrotate in debug mode does nothing"
                        " except printing debug messages!  Consider using verbose"
                        " mode (-v) instead if this is not what you want.\n\n");
                /* fallthrough */
            case 'v':
                logSetLevel(MESS_DEBUG);
                break;
            case 'l':
                if (strcmp(logFile, "syslog") == 0) {
                    logToSyslog(1);
                }
                else {
                    logFd = fopen(logFile, "w");
                    if (!logFd) {
                        message(MESS_ERROR, "error opening log file %s: %s\n",
                                logFile, strerror(errno));
                        break;
                    }
                    logSetMessageFile(logFd);
                }
                break;
            case 'V':
                printf("logrotate %s\n", VERSION);
                printf("\n");
                printf("    Default mail command:       %s\n", DEFAULT_MAIL_COMMAND);
                printf("    Default compress command:   %s\n", COMPRESS_COMMAND);
                printf("    Default uncompress command: %s\n", UNCOMPRESS_COMMAND);
                printf("    Default compress extension: %s\n", COMPRESS_EXT);
                printf("    Default state file path:    %s\n", STATEFILE);
#ifdef WITH_ACL
                printf("    ACL support:                yes\n");
#else
                printf("    ACL support:                no\n");
#endif
#ifdef WITH_SELINUX
                printf("    SELinux support:            yes\n");
#else
                printf("    SELinux support:            no\n");
#endif
                poptFreeContext(optCon);
                exit(0);
            default:
                break;
        }
    }

    if (arg < -1) {
        fprintf(stderr, "logrotate: bad argument %s: %s\n",
                poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
                poptStrerror(rc));
        poptFreeContext(optCon);
        return 2;
    }

    files = poptGetArgs(optCon);
    if (!files) {
        fprintf(stderr, "logrotate " VERSION
                " - Copyright (C) 1995-2001 Red Hat, Inc.\n");
        fprintf(stderr,
                "This may be freely redistributed under the terms of "
                "the GNU General Public License\n\n");
        poptPrintUsage(optCon, stderr, 0);
        poptFreeContext(optCon);
        exit(1);
    }
#ifdef WITH_SELINUX
    selinux_enabled = (is_selinux_enabled() > 0);
    selinux_enforce = security_getenforce();
#endif

    TAILQ_INIT(&logs);

    if (readAllConfigPaths(files))
        rc = 1;

    poptFreeContext(optCon);
    nowSecs = time(NULL);

    if (!debug && lockState(stateFile, skip_state_lock)) {
        exit(3);
    }

    if (readState(stateFile))
        rc = 1;

    message(MESS_DEBUG, "\nHandling %d logs\n", numLogs);

    for (log = logs.tqh_first; log != NULL; log = log->list.tqe_next)
        rc |= rotateLogSet(log, force);

    if (!debug)
        rc |= writeState(stateFile);

    return (rc != 0);
}

/* vim: set et sw=4 ts=4: */
