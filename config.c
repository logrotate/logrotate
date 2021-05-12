#include "queue.h"
#include <limits.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <grp.h>
#include <popt.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <wchar.h>
#include <wctype.h>
#include <fnmatch.h>
#include <sys/mman.h>
#include <libgen.h>

#if !defined(PATH_MAX) && defined(__FreeBSD__)
#include <sys/param.h>
#endif

#include "log.h"
#include "logrotate.h"

struct logInfoHead logs;

#if !defined(GLOB_ABORTED) && defined(GLOB_ABEND)
#define GLOB_ABORTED GLOB_ABEND
#endif

#define REALLOC_STEP            10
#define GLOB_STR_REALLOC_STEP   0x100

#if defined(SunOS) && !defined(isblank)
#define isblank(c) ( ( (c) == ' ' || (c) == '\t' ) ? 1 : 0 )
#endif

#ifdef __hpux
#include "asprintf.c"
#endif

#if !defined(HAVE_SECURE_GETENV)
#define secure_getenv getenv
#endif

#if !defined(HAVE_ASPRINTF) && !defined(_FORTIFY_SOURCE)
#include <stdarg.h>

int asprintf(char **string_ptr, const char *format, ...)
{
    va_list arg;
    char *str;
    int size;
    int rv;

    va_start(arg, format);
    size = vsnprintf(NULL, 0, format, arg);
    va_end(arg);
    if (size < 0) {
        return -1;
    }
    str = malloc((size_t)size + 1);
    if (str == NULL) {
        return -1;
    }
    va_start(arg, format);
    rv = vsnprintf(str, (size_t)size + 1, format, arg);
    va_end(arg);

    *string_ptr = str;
    return rv;
}

#endif

#if !defined(HAVE_STRNDUP)
char *strndup(const char *s, size_t n)
{
    size_t nAvail;
    char *p;

    /* min() */
    nAvail = strlen(s) + 1;
    if ( (n + 1) < nAvail)
        nAvail = n + 1;

    p = malloc(nAvail);
    if (!p)
        return NULL;
    memcpy(p, s, nAvail);
    p[nAvail - 1] = 0;
    return p;
}
#endif

/* list of compression commands and the corresponding file extensions */
struct compress_cmd_item {
    const char *cmd;
    const char *ext;
};
static const struct compress_cmd_item compress_cmd_list[] = {
    {"gzip", ".gz"},
    {"bzip2", ".bz2"},
    {"xz", ".xz"},
    {"zstd", ".zst"},
    {"compress", ".Z"},
    {"zip", "zip"},
};
static const unsigned compress_cmd_list_size = sizeof(compress_cmd_list)
    / sizeof(compress_cmd_list[0]);

enum {
    STATE_DEFAULT = 2,
    STATE_SKIP_LINE = 4,
    STATE_DEFINITION_END = 8,
    STATE_SKIP_CONFIG = 16,
    STATE_LOAD_SCRIPT = 32,
    STATE_ERROR = 64,
};

static const char *defTabooExts[] = {
    ",v",
    ".bak",
    ".cfsaved",
    ".disabled",
    ".dpkg-bak",
    ".dpkg-del",
    ".dpkg-dist",
    ".dpkg-new",
    ".dpkg-old",
    ".dpkg-tmp",
    ".rhn-cfg-tmp-*",
    ".rpmnew",
    ".rpmorig",
    ".rpmsave",
    ".swp",
    ".ucf-dist",
    ".ucf-new",
    ".ucf-old",
    "~"
};
static const unsigned defTabooCount = sizeof(defTabooExts) / sizeof(char *);

/* I shouldn't use globals here :-( */
static char **tabooPatterns = NULL;
static unsigned tabooCount = 0;
static int glob_errno = 0;

static int readConfigFile(const char *configFile, struct logInfo *defConfig);
static int globerr(const char *pathname, int theerr);

static char *isolateLine(char **strt, char **buf, size_t length) {
    char *endtag, *start, *tmp;
    const char *max = *buf + length;
    char *key;

    start = *strt;
    endtag = start;
    while (endtag < max && *endtag != '\n') {
        endtag++;}
    if (max < endtag)
        return NULL;
    tmp = endtag - 1;
    while (isspace((unsigned char)*endtag))
        endtag--;
    key = strndup(start, (size_t)(endtag - start + 1));
    if (key == NULL) {
        message_OOM();
        return NULL;
    }
    *strt = tmp;
    return key;
}

static char *isolateValue(const char *fileName, int lineNum, const char *key,
                          char **startPtr, char **buf, size_t length)
{
    char *chptr = *startPtr;
    const char *max = *startPtr + length;

    while (chptr < max && isblank((unsigned char)*chptr))
        chptr++;
    if (chptr < max && *chptr == '=') {
        chptr++;
        while ( chptr < max && isblank((unsigned char)*chptr))
            chptr++;
    }

    if (chptr < max && *chptr == '\n') {
        message(MESS_ERROR, "%s:%d argument expected after %s\n",
                fileName, lineNum, key);
        return NULL;
    }

    *startPtr = chptr;
    return isolateLine(startPtr, buf, length);
}

static char *isolateWord(char **strt, char **buf, size_t length) {
    char *endtag, *start;
    const char *max = *buf + length;
    char *key;
    start = *strt;
    while (start < max && isblank((unsigned char)*start))
        start++;
    endtag = start;
    while (endtag < max && isalpha((unsigned char)*endtag)) {
        endtag++;}
    if (max < endtag)
        return NULL;
    key = strndup(start, (size_t)(endtag - start));
    if (key == NULL) {
        message_OOM();
        return NULL;
    }
    *strt = endtag;
    return key;
}

static char *readPath(const char *configFile, int lineNum, const char *key,
                      char **startPtr, char **buf, size_t length)
{
    char *path = isolateValue(configFile, lineNum, key, startPtr, buf, length);
    if (path != NULL) {
        wchar_t pwc;
        size_t len;
        const char *chptr = path;

        while (*chptr && (len = mbrtowc(&pwc, chptr, strlen(chptr), NULL)) != 0) {
            if (len == (size_t)(-1) || len == (size_t)(-2) || !iswprint((wint_t)pwc) || iswblank((wint_t)pwc)) {
                message(MESS_ERROR, "%s:%d bad %s path %s\n",
                        configFile, lineNum, key, path);
                free(path);
                return NULL;
            }
            chptr += len;
        }
    }
    return path;
}

/* set *pUid to UID of the given user, return non-zero on failure */
static int resolveUid(const char *userName, uid_t *pUid)
{
    const struct passwd *pw;
    char *endptr;
    unsigned long int parsed_uid;

#ifdef __CYGWIN__
    if (strcmp(userName, "root") == 0) {
        *pUid = 0;
        return 0;
    }
#endif

    pw = getpwnam(userName);
    if (pw) {
        *pUid = pw->pw_uid;
        return 0;
    }

    parsed_uid = strtoul(userName, &endptr, 10);
    if (userName[0] != '\0' &&
        *endptr == '\0' &&
        parsed_uid < INT_MAX && /* parsed_uid != ULONG_MAX && */
        getpwuid((uid_t)parsed_uid) != NULL) {

        *pUid = (uid_t)parsed_uid;
        return 0;
    }

    return -1;
}

/* set *pGid to GID of the given group, return non-zero on failure */
static int resolveGid(const char *groupName, gid_t *pGid)
{
    const struct group *gr;
    char *endptr;
    unsigned long int parsed_gid;

#ifdef __CYGWIN__
    if (strcmp(groupName, "root") == 0) {
        *pGid = 0;
        return 0;
    }
#endif

    gr = getgrnam(groupName);
    if (gr) {
        *pGid = gr->gr_gid;
        return 0;
    }

    parsed_gid = strtoul(groupName, &endptr, 10);
    if (groupName[0] != '\0' &&
        *endptr == '\0' &&
        parsed_gid < INT_MAX && /* parsed_gid != ULONG_MAX && */
        getgrgid((gid_t)parsed_gid) != NULL) {

        *pGid = (gid_t)parsed_gid;
        return 0;
    }

    return -1;
}

static int readModeUidGid(const char *configFile, int lineNum, const char *key,
                          const char *directive, mode_t *mode, uid_t *pUid,
                          gid_t *pGid)
{
    char u[200], g[200];
    mode_t m = 0;
    char tmp;
    int rc;

    if (!strcmp("su", directive))
        /* do not read <mode> for the 'su' directive */
        rc = 0;
    else {
        unsigned short int parsed_mode;
        rc = sscanf(key, "%ho %199s %199s%c", &parsed_mode, u, g, &tmp);
        m = parsed_mode;
    }

    /* We support 'key <owner> <group> notation now */
    if (rc == 0) {
        rc = sscanf(key, "%199s %199s%c", u, g, &tmp);
        /* Simulate that we have read mode and keep the default value. */
        if (rc > 0) {
            m = *mode;
            rc += 1;
        }
    }

    if (rc == 4) {
        message(MESS_ERROR, "%s:%d extra arguments for "
                "%s\n", configFile, lineNum, directive);
        return -1;
    }

    if (rc > 0) {
        *mode = m;
    }

    if (rc > 1) {
        if (resolveUid(u, pUid) != 0) {
            message(MESS_ERROR, "%s:%d unknown user '%s'\n",
                    configFile, lineNum, u);
            return -1;
        }
    }
    if (rc > 2) {
        if (resolveGid(g, pGid) != 0) {
            message(MESS_ERROR, "%s:%d unknown group '%s'\n",
                    configFile, lineNum, g);
            return -1;
        }
    }

    return 0;
}

static char *readAddress(const char *configFile, int lineNum, const char *key,
                         char **startPtr, char **buf, size_t length)
{
    char *start = *startPtr;
    char *address = isolateValue(configFile, lineNum, key, startPtr, buf, length);

    if (address != NULL) {
        /* validate the address */
        const char *chptr = address;
        while (isprint((unsigned char) *chptr) && *chptr != ' ') {
            chptr++;
        }

        if (*chptr) {
            message(MESS_ERROR, "%s:%d bad %s address %s\n",
                    configFile, lineNum, key, start);
            free(address);
            return NULL;
        }
    }

    return address;
}

static int do_mkdir(const char *path, mode_t mode, uid_t uid, gid_t gid) {
    if (mkdir(path, mode) == 0) {
        /* newly created directory, set the owner and permissions */
        if (chown(path, uid, gid) != 0) {
            message(MESS_ERROR, "error setting owner of %s to uid %u and gid %u: %s\n",
                    path, (unsigned) uid, (unsigned) gid, strerror(errno));
            return -1;
        }

        if (chmod(path, mode) != 0) {
            message(MESS_ERROR, "error setting permissions of %s to 0%o: %s\n",
                    path, mode, strerror(errno));
            return -1;
        }

        return 0;
    }

    if (errno == EEXIST) {
        /* path already exists, check whether it is a directory or not */
        struct stat sb;
        if ((stat(path, &sb) == 0) && S_ISDIR(sb.st_mode))
            return 0;

        message(MESS_ERROR, "path %s already exists, but it is not a directory\n", path);
        errno = ENOTDIR;
        return -1;
    }

    message(MESS_ERROR, "error creating %s: %s\n", path, strerror(errno));
    return -1;
}

static int mkpath(const char *path, mode_t mode, uid_t uid, gid_t gid) {
    char *pp;
    char *sp;
    int rv;
    char *copypath = strdup(path);

    if (!copypath) {
        message_OOM();
        return 1;
    }

    rv = 0;
    pp = copypath;
    while (rv == 0 && (sp = strchr(pp, '/')) != NULL) {
        if (sp != pp) {
            *sp = '\0';
            rv = do_mkdir(copypath, mode, uid, gid);
            *sp = '/';
        }
        pp = sp + 1;
    }
    if (rv == 0) {
        rv = do_mkdir(path, mode, uid, gid);
    }
    free(copypath);
    return rv;
}

static int checkFile(const char *fname)
{
    unsigned i;

    /* Check if fname is '.' or '..'; if so, return false */
    if (fname[0] == '.' && (!fname[1] || (fname[1] == '.' && !fname[2])))
        return 0;

    /* Check if fname is ending in a taboo-extension; if so, return false */
    for (i = 0; i < tabooCount; i++) {
        const char *pattern = tabooPatterns[i];
        if (!fnmatch(pattern, fname, FNM_PERIOD))
        {
            message(MESS_DEBUG, "Ignoring %s, because of %s pattern match\n",
                    fname, pattern);
            return 0;
        }
    }
    /* All checks have been passed; return true */
    return 1;
}

/* Used by qsort to sort filelist */
static int compar(const void *p, const void *q)
{
    return strcoll(*((char * const*) p), *((char * const*) q));
}

/* Free memory blocks pointed to by pointers in a 2d array and the array itself */
static void free_2d_array(char **array, unsigned lines_count)
{
    unsigned i;
    for (i = 0; i < lines_count; ++i)
        free(array[i]);
    free(array);
}

#define MEMBER_COPY(dest, src) \
    do { \
        if ((src) && rv == 0) { \
            (dest) = strdup(src); \
            if ((dest) == NULL) { \
                message_OOM(); \
                rv = 1; \
            } \
        } else { \
            (dest) = NULL; \
        } \
    } while (0)
static int copyLogInfo(struct logInfo *to, const struct logInfo *from)
{
    int rv = 0;

    memset(to, 0, sizeof(*to));
    MEMBER_COPY(to->oldDir, from->oldDir);
    to->criterium = from->criterium;
    to->weekday = from->weekday;
    to->threshold = from->threshold;
    to->minsize = from->minsize;
    to->maxsize = from->maxsize;
    to->rotateCount = from->rotateCount;
    to->rotateMinAge = from->rotateMinAge;
    to->rotateAge = from->rotateAge;
    to->logStart = from->logStart;
    MEMBER_COPY(to->pre, from->pre);
    MEMBER_COPY(to->post, from->post);
    MEMBER_COPY(to->first, from->first);
    MEMBER_COPY(to->last, from->last);
    MEMBER_COPY(to->preremove, from->preremove);
    MEMBER_COPY(to->logAddress , from->logAddress);
    MEMBER_COPY(to->extension, from->extension);
    MEMBER_COPY(to->compress_prog, from->compress_prog);
    MEMBER_COPY(to->uncompress_prog, from->uncompress_prog);
    MEMBER_COPY(to->compress_ext, from->compress_ext);
    to->flags = from->flags;
    to->shred_cycles = from->shred_cycles;
    to->createMode = from->createMode;
    to->createUid = from->createUid;
    to->createGid = from->createGid;
    to->suUid = from->suUid;
    to->suGid = from->suGid;
    to->olddirMode = from->olddirMode;
    to->olddirUid = from->olddirUid;
    to->olddirGid = from->olddirGid;

    if (from->compress_options_count) {
        poptDupArgv(from->compress_options_count, from->compress_options_list,
                    &to->compress_options_count,  &to->compress_options_list);
        if (to->compress_options_list == NULL) {
            message_OOM();
            rv = 1;
        }
    }

    MEMBER_COPY(to->dateformat, from->dateformat);

    to->list = from->list;

    return rv;
}
#undef MEMBER_COPY

static void freeLogInfo(struct logInfo *log)
{
    free(log->pattern);
    free_2d_array(log->files, log->numFiles);
    free(log->oldDir);
    free(log->pre);
    free(log->post);
    free(log->first);
    free(log->last);
    free(log->preremove);
    free(log->logAddress);
    free(log->extension);
    free(log->compress_prog);
    free(log->uncompress_prog);
    free(log->compress_ext);
    free(log->compress_options_list);
    free(log->dateformat);
}

static struct logInfo *newLogInfo(const struct logInfo *template)
{
    struct logInfo *new;

    new = malloc(sizeof(*new));
    if (new == NULL) {
        message_OOM();
        return NULL;
    }

    if (copyLogInfo(new, template)) {
        freeLogInfo(new);
        free(new);
        return NULL;
    }

    TAILQ_INSERT_TAIL(&logs, new, list);
    numLogs++;

    return new;
}

static void removeLogInfo(struct logInfo *log)
{
    if (log == NULL)
        return;

    freeLogInfo(log);
    TAILQ_REMOVE(&logs, log, list);
    free(log);
    numLogs--;
}

static void freeTailLogs(int num)
{
    message(MESS_DEBUG, "removing last %d log configs\n", num);

    while (num--)
        removeLogInfo(TAILQ_LAST(&logs, logInfoHead));

}

static const char *crit_to_string(enum criterium crit)
{
    switch (crit) {
        case ROT_HOURLY:    return "hourly";
        case ROT_DAYS:      return "daily";
        case ROT_WEEKLY:    return "weekly";
        case ROT_MONTHLY:   return "monthly";
        case ROT_YEARLY:    return "yearly";
        case ROT_SIZE:      return "size";
        default:            return "XXX";
    }
}

static void set_criterium(enum criterium *pDst, enum criterium src, int *pSet)
{
    if (*pSet && (*pDst != src)) {
        /* we are overriding a previously set criterium */
        message(MESS_VERBOSE, "warning: '%s' overrides previously specified '%s'\n",
                crit_to_string(src), crit_to_string(*pDst));
    }
    *pDst = src;
    *pSet = 1;
}

static int readConfigPath(const char *path, struct logInfo *defConfig)
{
    struct stat sb;
    int result = 0;
    struct logInfo defConfigBackup;

    if (stat(path, &sb)) {
        message(MESS_ERROR, "cannot stat %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (S_ISDIR(sb.st_mode)) {
        char **namelist = NULL;
        struct dirent *dp;
        int here;
        unsigned files_count = 0, i;
        DIR *dirp;

        if ((here = open(".", O_RDONLY)) == -1) {
            message(MESS_ERROR, "cannot open current directory: %s\n",
                    strerror(errno));
            return 1;
        }

        if ((dirp = opendir(path)) == NULL) {
            message(MESS_ERROR, "cannot open directory %s: %s\n", path,
                    strerror(errno));
            close(here);
            return 1;
        }
        while ((dp = readdir(dirp)) != NULL) {
            if (checkFile(dp->d_name)) {
                /* Realloc memory for namelist array if necessary */
                if (files_count % REALLOC_STEP == 0) {
                    char **p = (char **) realloc(namelist,
                            (files_count +
                             REALLOC_STEP) * sizeof(char *));
                    if (p) {
                        namelist = p;
                        memset(namelist + files_count, '\0',
                               REALLOC_STEP * sizeof(char *));
                    } else {
                        free_2d_array(namelist, files_count);
                        closedir(dirp);
                        close(here);
                        message_OOM();
                        return 1;
                    }
                }
                /* Alloc memory for file name */
                namelist[files_count] = strdup(dp->d_name);
                if (namelist[files_count] != NULL) {
                    files_count++;
                } else {
                    free_2d_array(namelist, files_count);
                    closedir(dirp);
                    close(here);
                    message_OOM();
                    return 1;
                }
            }
        }
        closedir(dirp);

        if (files_count > 0) {
            qsort(namelist, files_count, sizeof(char *), compar);
        } else {
            close(here);
            return 0;
        }

        if (chdir(path)) {
            message(MESS_ERROR, "error in chdir(\"%s\"): %s\n", path,
                    strerror(errno));
            close(here);
            free_2d_array(namelist, files_count);
            return 1;
        }

        for (i = 0; i < files_count; ++i) {
            assert(namelist[i] != NULL);
            if (copyLogInfo(&defConfigBackup, defConfig)) {
                freeLogInfo(&defConfigBackup);
                close(here);
                free_2d_array(namelist, files_count);
                return 1;
            }
            if (readConfigFile(namelist[i], defConfig)) {
                message(MESS_ERROR, "found error in file %s, skipping\n", namelist[i]);
                freeLogInfo(defConfig);
                if (copyLogInfo(defConfig, &defConfigBackup)){} /* do not check, we are already in a error path */
                freeLogInfo(&defConfigBackup);
                result = 1;
                continue;
            }
            freeLogInfo(&defConfigBackup);
        }

        if (fchdir(here) < 0) {
            message(MESS_ERROR, "could not change directory to '.': %s\n", strerror(errno));
        }
        close(here);
        free_2d_array(namelist, files_count);
    } else {
        if (copyLogInfo(&defConfigBackup, defConfig)) {
            freeLogInfo(&defConfigBackup);
            return 1;
        }

        if (readConfigFile(path, defConfig)) {
            freeLogInfo(defConfig);
            if (copyLogInfo(defConfig, &defConfigBackup)){} /* do not check, we are already in a error path */
            result = 1;
        }
        freeLogInfo(&defConfigBackup);
    }

    return result;
}

int readAllConfigPaths(const char **paths)
{
    int result = 0;
    unsigned i;
    const char **file;
    struct logInfo defConfig = {
        .pattern = NULL,
        .files = NULL,
        .numFiles = 0,
        .oldDir = NULL,
        .criterium = ROT_SIZE,
        .threshold = 1024 * 1024,
        .minsize = 0,
        .maxsize = 0,
        .rotateCount = 0,
        .rotateMinAge = 0,
        .rotateAge = 0,
        .logStart = -1,
        .pre = NULL,
        .post = NULL,
        .first = NULL,
        .last = NULL,
        .preremove = NULL,
        .logAddress = NULL,
        .extension = NULL,
        .addextension = NULL,
        .compress_prog = NULL,
        .uncompress_prog = NULL,
        .compress_ext = NULL,
        .dateformat = NULL,
        .flags = LOG_FLAG_IFEMPTY,
        .shred_cycles = 0,
        .createMode = NO_MODE,
        .createUid = NO_UID,
        .createGid = NO_GID,
        .olddirMode = NO_MODE,
        .olddirUid = NO_UID,
        .olddirGid = NO_GID,
        .suUid = NO_UID,
        .suGid = NO_GID,
        .compress_options_list = NULL,
        .compress_options_count = 0
    };

    tabooPatterns = malloc(sizeof(*tabooPatterns) * defTabooCount);
    if (tabooPatterns == NULL) {
        message_OOM();
        return 1;
    }


    for (i = 0; i < defTabooCount; i++) {
        char *pattern = NULL;

        /* generate a pattern by concatenating star (wildcard) to the
         * suffix literal
         */
        if (asprintf(&pattern, "*%s", defTabooExts[i]) < 0) {
            free_2d_array(tabooPatterns, tabooCount);
            message_OOM();
            return 1;
        }

        tabooPatterns[i] = pattern;
        tabooCount++;
    }

    for (file = paths; *file; file++) {
        if (readConfigPath(*file, &defConfig))
            result = 1;
    }
    free_2d_array(tabooPatterns, tabooCount);
    freeLogInfo(&defConfig);
    return result;
}

static char* parseGlobString(const char *configFile, int lineNum,
                             const char *buf, size_t length, char **ppos)
{
    /* output buffer */
    char *globString = NULL;
    size_t globStringPos = 0;
    size_t globStringAlloc = 0;
    enum {
        PGS_INIT,   /* picking blanks, looking for '#' */
        PGS_DATA,   /* picking data, looking for end of line */
        PGS_COMMENT /* skipping comment, looking for end of line */
    } state = PGS_INIT;

    /* move the cursor at caller's side while going through the input */
    for (; ((size_t)(*ppos - buf) < length) && **ppos; (*ppos)++) {
        /* state transition (see above) */
        switch (state) {
            case PGS_INIT:
                if ('#' == **ppos)
                    state = PGS_COMMENT;
                else if (!isspace((unsigned char) **ppos))
                    state = PGS_DATA;
                break;

            default:
                if ('\n' == **ppos)
                    state = PGS_INIT;
        }

        if (PGS_COMMENT == state)
            /* skip comment */
            continue;

        switch (**ppos) {
            case '}':
                message(MESS_ERROR, "%s:%d unexpected } (missing previous '{')\n", configFile, lineNum);
                free(globString);
                return NULL;

            case '{':
                /* NUL-terminate globString */
                assert(globStringPos < globStringAlloc);
                globString[globStringPos] = '\0';
                return globString;

            default:
                break;
        }

        /* grow the output buffer if needed */
        if (globStringPos + 2 > globStringAlloc) {
            char *ptr;
            globStringAlloc += GLOB_STR_REALLOC_STEP;
            ptr = realloc(globString, globStringAlloc);
            if (!ptr) {
                message_OOM();
                free(globString);
                return NULL;
            }
            globString = ptr;
        }

        /* copy a single character */
        globString[globStringPos++] = **ppos;
    }

    /* premature end of input */
    message(MESS_ERROR, "%s:%d missing '{' after log files definition\n", configFile, lineNum);
    free(globString);
    return NULL;
}

static int globerr(const char *pathname, int theerr)
{
    (void) pathname;

    /* prevent glob() from being aborted in certain cases */
    switch (theerr) {
        case ENOTDIR:
            /* non-directory where directory was expected by the glob */
            return 0;

        case ENOENT:
            /* most likely symlink with non-existent target */
            return 0;

        default:
            break;
    }

    glob_errno = theerr;

    /* We want the glob operation to abort on error, so return 1 */
    return 1;
}

#define freeLogItem(what) \
    do { \
        free(newlog->what); \
        newlog->what = NULL; \
    } while (0)
#define RAISE_ERROR() \
    do { \
        if (newlog != defConfig) { \
            state = STATE_ERROR; \
            goto next_state; \
        } else { \
            goto error; \
        } \
    } while(0)
#define MAX_NESTING 16U

static int readConfigFile(const char *configFile, struct logInfo *defConfig)
{
    int fd;
    char *buf, *key = NULL;
    size_t length;
    int lineNum = 1;
    char *scriptStart = NULL;
    char **scriptDest = NULL;
    struct logInfo *newlog = defConfig;
    char *start, *chptr;
    struct stat sb;
    int state = STATE_DEFAULT;
    int logerror = 0;
    /* to check if incompatible criteria are specified */
    int criterium_set = 0;
    static unsigned recursion_depth = 0U;
    char *globerr_msg = NULL;
    int in_config = 0;
#ifdef HAVE_MADVISE
    int r;
#endif
    struct flock fd_lock = {
        .l_start = 0,
        .l_len = 0,
        .l_whence = SEEK_SET,
        .l_type = F_RDLCK
    };

    fd = open(configFile, O_RDONLY);
    if (fd < 0) {
        message(MESS_ERROR, "failed to open config file %s: %s\n",
                configFile, strerror(errno));
        return 1;
    }
    /* We don't want anybody to change the file while we parse it,
     * let's try to lock it for reading. */
    if (fcntl(fd, F_SETLK, &fd_lock) == -1) {
        message(MESS_ERROR, "Could not lock file %s for reading\n",
                configFile);
    }
    if (fstat(fd, &sb)) {
        message(MESS_ERROR, "fstat of %s failed: %s\n", configFile,
                strerror(errno));
        close(fd);
        return 1;
    }
    if (!S_ISREG(sb.st_mode)) {
        message(MESS_DEBUG,
                "Ignoring %s because it's not a regular file.\n",
                configFile);
        close(fd);
        return 0;
    }

    if (!getpwuid(getuid())) {
        message(MESS_ERROR, "Cannot find logrotate UID (%d) in passwd file: %s\n",
                getuid(), strerror(errno));
        close(fd);
        return 1;
    }

    if (getuid() == ROOT_UID) {
        if ((sb.st_mode & 07533) != 0400) {
            message(MESS_NORMAL,
                    "Potentially dangerous mode on %s: 0%o\n",
                    configFile, (unsigned) (sb.st_mode & 07777));
        }

        if (sb.st_mode & 0022) {
            message(MESS_ERROR,
                    "Ignoring %s because it is writable by group or others.\n",
                    configFile);
            close(fd);
            return 0;
        }

        if (sb.st_uid != ROOT_UID) {
            message(MESS_ERROR,
                    "Ignoring %s because the file owner is wrong (should be root or user with uid 0).\n",
                    configFile);
            close(fd);
            return 0;
        }
    }

    length = (size_t)sb.st_size;

    if (length > 0xffffff) {
        message(MESS_ERROR, "file %s too large, probably not a config file.\n",
                configFile);
        close(fd);
        return 1;
    }

    /* We can't mmap empty file... */
    if (length == 0) {
        message(MESS_DEBUG,
                "Ignoring %s because it's empty.\n",
                configFile);
        close(fd);
        return 0;
    }

#ifdef MAP_POPULATE
    buf = mmap(NULL, length, PROT_READ,
            MAP_PRIVATE | MAP_POPULATE, fd, (off_t) 0);
#else /* MAP_POPULATE */
    buf = mmap(NULL, length, PROT_READ,
            MAP_PRIVATE, fd, (off_t) 0);
#endif /* MAP_POPULATE */

    if (buf == MAP_FAILED) {
        message(MESS_ERROR, "Error mapping config file %s: %s\n",
                configFile, strerror(errno));
        close(fd);
        return 1;
    }

#ifdef HAVE_MADVISE
#ifdef MADV_DONTFORK
    r = madvise(buf, length, MADV_SEQUENTIAL | MADV_WILLNEED | MADV_DONTFORK);
#else /* MADV_DONTFORK */
    r = madvise(buf, length, MADV_SEQUENTIAL | MADV_WILLNEED);
#endif /* MADV_DONTFORK */
    if (r < 0) {
        message(MESS_DEBUG, "Failed to advise use of memory: %s\n",
                strerror(errno));
    }
#endif /* HAVE_MADVISE */

    message(MESS_DEBUG, "reading config file %s\n", configFile);

    for (start = buf; (size_t)(start - buf) < length; start++) {
        switch (state) {
            case STATE_DEFAULT:
                if (isblank((unsigned char)*start))
                    continue;
                /* Skip comment */
                if (*start == '#') {
                    state = STATE_SKIP_LINE;
                    continue;
                }

                if (isalpha((unsigned char)*start)) {
                    free(key);
                    key = isolateWord(&start, &buf, length);
                    if (key == NULL) {
                        message(MESS_ERROR, "%s:%d failed to parse keyword\n",
                                configFile, lineNum);
                        continue;
                    }
                    if (!isspace((unsigned char)*start)) {
                        message(MESS_NORMAL, "%s:%d keyword '%s' not properly"
                                " separated, found %#x\n",
                                configFile, lineNum, key, *start);
                    }
                    if (!strcmp(key, "compress")) {
                        newlog->flags |= LOG_FLAG_COMPRESS;
                    } else if (!strcmp(key, "nocompress")) {
                        newlog->flags &= ~LOG_FLAG_COMPRESS;
                    } else if (!strcmp(key, "delaycompress")) {
                        newlog->flags |= LOG_FLAG_DELAYCOMPRESS;
                    } else if (!strcmp(key, "nodelaycompress")) {
                        newlog->flags &= ~LOG_FLAG_DELAYCOMPRESS;
                    } else if (!strcmp(key, "shred")) {
                        newlog->flags |= LOG_FLAG_SHRED;
                    } else if (!strcmp(key, "noshred")) {
                        newlog->flags &= ~LOG_FLAG_SHRED;
                    } else if (!strcmp(key, "sharedscripts")) {
                        newlog->flags |= LOG_FLAG_SHAREDSCRIPTS;
                    } else if (!strcmp(key, "nosharedscripts")) {
                        newlog->flags &= ~LOG_FLAG_SHAREDSCRIPTS;
                    } else if (!strcmp(key, "copytruncate")) {
                        newlog->flags |= LOG_FLAG_COPYTRUNCATE;
                        newlog->flags &= ~LOG_FLAG_TMPFILENAME;
                    } else if (!strcmp(key, "nocopytruncate")) {
                        newlog->flags &= ~LOG_FLAG_COPYTRUNCATE;
                    } else if (!strcmp(key, "renamecopy")) {
                        newlog->flags |= LOG_FLAG_TMPFILENAME;
                        newlog->flags &= ~LOG_FLAG_COPYTRUNCATE;
                    } else if (!strcmp(key, "norenamecopy")) {
                        newlog->flags &= ~LOG_FLAG_TMPFILENAME;
                    } else if (!strcmp(key, "copy")) {
                        newlog->flags |= LOG_FLAG_COPY;
                    } else if (!strcmp(key, "nocopy")) {
                        newlog->flags &= ~LOG_FLAG_COPY;
                    } else if (!strcmp(key, "ifempty")) {
                        newlog->flags |= LOG_FLAG_IFEMPTY;
                    } else if (!strcmp(key, "notifempty")) {
                        newlog->flags &= ~LOG_FLAG_IFEMPTY;
                    } else if (!strcmp(key, "dateext")) {
                        newlog->flags |= LOG_FLAG_DATEEXT;
                    } else if (!strcmp(key, "nodateext")) {
                        newlog->flags &= ~LOG_FLAG_DATEEXT;
                    } else if (!strcmp(key, "dateyesterday")) {
                        newlog->flags |= LOG_FLAG_DATEYESTERDAY;
                    } else if (!strcmp(key, "datehourago")) {
                        newlog->flags |= LOG_FLAG_DATEHOURAGO;
                    } else if (!strcmp(key, "dateformat")) {
                        freeLogItem(dateformat);
                        newlog->dateformat = isolateValue(configFile, lineNum,
                                                          key, &start, &buf,
                                                          length);
                    } else if (!strcmp(key, "noolddir")) {
                        freeLogItem(oldDir);
                    } else if (!strcmp(key, "mailfirst")) {
                        newlog->flags |= LOG_FLAG_MAILFIRST;
                    } else if (!strcmp(key, "maillast")) {
                        newlog->flags &= ~LOG_FLAG_MAILFIRST;
                    } else if (!strcmp(key, "su")) {
                        int rv;
                        mode_t tmp_mode = NO_MODE;
                        free(key);
                        key = isolateLine(&start, &buf, length);
                        if (key == NULL) {
                            message(MESS_ERROR, "%s:%d failed to parse su option value\n",
                                    configFile, lineNum);
                            RAISE_ERROR();
                        }

                        rv = readModeUidGid(configFile, lineNum, key, "su",
                                            &tmp_mode, &newlog->suUid,
                                            &newlog->suGid);
                        if (rv == -1) {
                            RAISE_ERROR();
                        }
                        else if (tmp_mode != NO_MODE) {
                            message(MESS_ERROR, "%s:%d extra arguments for "
                                    "su\n", configFile, lineNum);
                            RAISE_ERROR();
                        }
                        else if (newlog->suUid == NO_UID) {
                            message(MESS_ERROR, "%s:%d no user for "
                                    "su\n", configFile, lineNum);
                            RAISE_ERROR();
                        }
                        else if (newlog->suGid == NO_GID) {
                            message(MESS_ERROR, "%s:%d no group for "
                                    "su\n", configFile, lineNum);
                            RAISE_ERROR();
                        }

                        newlog->flags |= LOG_FLAG_SU;
                    } else if (!strcmp(key, "create")) {
                        int rv;

                        free(key);
                        key = isolateLine(&start, &buf, length);
                        if (key == NULL)
                            continue;

                        rv = readModeUidGid(configFile, lineNum, key, "create",
                                            &newlog->createMode, &newlog->createUid,
                                            &newlog->createGid);
                        if (rv == -1) {
                            RAISE_ERROR();
                        }

                        newlog->flags |= LOG_FLAG_CREATE;
                    } else if (!strcmp(key, "createolddir")) {
                        int rv;

                        free(key);
                        key = isolateLine(&start, &buf, length);
                        if (key == NULL)
                            continue;

                        rv = readModeUidGid(configFile, lineNum, key, "createolddir",
                                            &newlog->olddirMode, &newlog->olddirUid,
                                            &newlog->olddirGid);
                        if (rv == -1) {
                            RAISE_ERROR();
                        }

                        newlog->flags |= LOG_FLAG_OLDDIRCREATE;
                    } else if (!strcmp(key, "nocreateolddir")) {
                        newlog->flags &= ~LOG_FLAG_OLDDIRCREATE;
                    } else if (!strcmp(key, "nocreate")) {
                        newlog->flags &= ~LOG_FLAG_CREATE;
                    } else if (!strcmp(key, "size") || !strcmp(key, "minsize") ||
                            !strcmp(key, "maxsize")) {
                        char *opt = key;

                        key = isolateValue(configFile, lineNum, opt, &start, &buf, length);
                        if (key && key[0]) {
                            off_t size;
                            unsigned long multiplier;
                            const size_t l = strlen(key) - 1;
                            if (key[l] == 'k' || key[l] == 'K') {
                                key[l] = '\0';
                                multiplier = 1024;
                            } else if (key[l] == 'M') {
                                key[l] = '\0';
                                multiplier = 1024 * 1024;
                            } else if (key[l] == 'G') {
                                key[l] = '\0';
                                multiplier = 1024 * 1024 * 1024;
                            } else if (!isdigit((unsigned char)key[l])) {
                                free(opt);
                                message(MESS_ERROR, "%s:%d unknown unit '%c'\n",
                                        configFile, lineNum, key[l]);
                                RAISE_ERROR();
                            } else {
                                multiplier = 1;
                            }

                            size = (off_t) (multiplier * strtoull(key, &chptr, 0));
                            if (*chptr || size < 0) {
                                message(MESS_ERROR, "%s:%d bad size '%s'\n",
                                        configFile, lineNum, key);
                                free(opt);
                                RAISE_ERROR();
                            }
                            if (!strncmp(opt, "size", 4)) {
                                set_criterium(&newlog->criterium, ROT_SIZE, &criterium_set);
                                newlog->threshold = size;
                            } else if (!strncmp(opt, "maxsize", 7)) {
                                newlog->maxsize = size;
                            } else {
                                newlog->minsize = size;
                            }
                            free(opt);
                        }
                        else {
                            free(opt);
                            continue;
                        }
                    } else if (!strcmp(key, "shredcycles")) {
                        free(key);
                        key = isolateValue(configFile, lineNum, "shred cycles",
                                           &start, &buf, length);
                        if (key == NULL) {
                            RAISE_ERROR();
                        }
                        newlog->shred_cycles = (int)strtoul(key, &chptr, 0);
                        if (*chptr || newlog->shred_cycles < 0) {
                            message(MESS_ERROR, "%s:%d bad shred cycles '%s'\n",
                                    configFile, lineNum, key);
                            RAISE_ERROR();
                        }
                    } else if (!strcmp(key, "hourly")) {
                        set_criterium(&newlog->criterium, ROT_HOURLY, &criterium_set);
                    } else if (!strcmp(key, "daily")) {
                        set_criterium(&newlog->criterium, ROT_DAYS, &criterium_set);
                        newlog->threshold = 1;
                    } else if (!strcmp(key, "monthly")) {
                        set_criterium(&newlog->criterium, ROT_MONTHLY, &criterium_set);
                    } else if (!strcmp(key, "weekly")) {
                        unsigned weekday;
                        char tmp;
                        set_criterium(&newlog->criterium, ROT_WEEKLY, &criterium_set);
                        free(key);
                        key = isolateLine(&start, &buf, length);
                        if (key == NULL || key[0] == '\0') {
                            /* default to Sunday if no argument was given */
                            newlog->weekday = 0;
                            continue;
                        }

                        if (1 == sscanf(key, "%u%c", &weekday, &tmp) && weekday <= 7) {
                            /* use the selected weekday, 7 means "once per week" */
                            newlog->weekday = weekday;
                            continue;
                        }
                        message(MESS_ERROR, "%s:%d bad weekly directive '%s'\n",
                                configFile, lineNum, key);
                        goto error;
                    } else if (!strcmp(key, "yearly")) {
                        set_criterium(&newlog->criterium, ROT_YEARLY, &criterium_set);
                    } else if (!strcmp(key, "rotate")) {
                        free(key);
                        key = isolateValue(configFile, lineNum, "rotate count", &start,
                                           &buf, length);
                        if (key == NULL) {
                            RAISE_ERROR();
                        }
                        newlog->rotateCount = (int)strtol(key, &chptr, 0);
                        if (*chptr || newlog->rotateCount < -1) {
                            message(MESS_ERROR,
                                    "%s:%d bad rotation count '%s'\n",
                                    configFile, lineNum, key);
                            RAISE_ERROR();
                        }
                    } else if (!strcmp(key, "start")) {
                        free(key);
                        key = isolateValue(configFile, lineNum, "start count", &start,
                                           &buf, length);
                        if (key == NULL) {
                            RAISE_ERROR();
                        }
                        newlog->logStart = (int)strtoul(key, &chptr, 0);
                        if (*chptr || newlog->logStart < 0) {
                            message(MESS_ERROR, "%s:%d bad start count '%s'\n",
                                    configFile, lineNum, key);
                            RAISE_ERROR();
                        }
                    } else if (!strcmp(key, "minage")) {
                        free(key);
                        key = isolateValue(configFile, lineNum, "minage count", &start,
                                           &buf, length);
                        if (key == NULL) {
                            RAISE_ERROR();
                        }
                        newlog->rotateMinAge = (int)strtoul(key, &chptr, 0);
                        if (*chptr || newlog->rotateMinAge < 0) {
                            message(MESS_ERROR, "%s:%d bad minimum age '%s'\n",
                                    configFile, lineNum, start);
                            RAISE_ERROR();
                        }
                    } else if (!strcmp(key, "maxage")) {
                        free(key);
                        key = isolateValue(configFile, lineNum, "maxage count", &start,
                                           &buf, length);
                        if (key == NULL) {
                            RAISE_ERROR();
                        }
                        newlog->rotateAge = (int)strtoul(key, &chptr, 0);
                        if (*chptr || newlog->rotateAge < 0) {
                            message(MESS_ERROR, "%s:%d bad maximum age '%s'\n",
                                    configFile, lineNum, start);
                            RAISE_ERROR();
                        }
                    } else if (!strcmp(key, "errors")) {
                        message(MESS_NORMAL,
                                "%s: %d: the errors directive is deprecated and no longer used.\n",
                                configFile, lineNum);
                    } else if (!strcmp(key, "mail")) {
                        freeLogItem(logAddress);
                        if (!(newlog->logAddress = readAddress(configFile, lineNum,
                                        "mail", &start, &buf, length))) {
                            RAISE_ERROR();
                        }
                    } else if (!strcmp(key, "nomail")) {
                        freeLogItem(logAddress);
                    } else if (!strcmp(key, "missingok")) {
                        newlog->flags |= LOG_FLAG_MISSINGOK;
                    } else if (!strcmp(key, "nomissingok")) {
                        newlog->flags &= ~LOG_FLAG_MISSINGOK;
                    } else if (!strcmp(key, "prerotate")) {
                        freeLogItem (pre);
                        scriptStart = start;
                        scriptDest = &newlog->pre;
                        state = STATE_LOAD_SCRIPT;
                    } else if (!strcmp(key, "firstaction")) {
                        freeLogItem (first);
                        scriptStart = start;
                        scriptDest = &newlog->first;
                        state = STATE_LOAD_SCRIPT;
                    } else if (!strcmp(key, "postrotate")) {
                        freeLogItem (post);
                        scriptStart = start;
                        scriptDest = &newlog->post;
                        state = STATE_LOAD_SCRIPT;
                    } else if (!strcmp(key, "lastaction")) {
                        freeLogItem (last);
                        scriptStart = start;
                        scriptDest = &newlog->last;
                        state = STATE_LOAD_SCRIPT;
                    } else if (!strcmp(key, "preremove")) {
                        freeLogItem (preremove);
                        scriptStart = start;
                        scriptDest = &newlog->preremove;
                        state = STATE_LOAD_SCRIPT;
                    } else if (!strcmp(key, "tabooext")) {
                        char *endtag;

                        if (newlog != defConfig) {
                            message(MESS_ERROR,
                                    "%s:%d tabooext may not appear inside "
                                    "of log file definition\n", configFile,
                                    lineNum);
                            state = STATE_ERROR;
                            continue;
                        }
                        free(key);
                        key = isolateValue(configFile, lineNum, "tabooext", &start,
                                           &buf, length);
                        if (key == NULL)
                            continue;
                        endtag = key;
                        if (*endtag == '+') {
                            endtag++;
                            while (isspace((unsigned char)*endtag) && *endtag)
                                endtag++;
                        } else {
                            free_2d_array(tabooPatterns, tabooCount);
                            tabooCount = 0;
                            /* realloc of NULL is safe by definition */
                            tabooPatterns = NULL;
                        }

                        while (*endtag) {
                            char *pattern = NULL;

                            chptr = endtag;
                            while (!isspace((unsigned char)*chptr) && *chptr != ',' && *chptr)
                                chptr++;

                            /* accept only non-empty patterns to avoid exclusion of everything */
                            if (endtag < chptr) {
                                char **tmp = realloc(tabooPatterns, sizeof(*tabooPatterns) *
                                        (tabooCount + 1));
                                if (tmp == NULL) {
                                    message_OOM();
                                    RAISE_ERROR();
                                }
                                tabooPatterns = tmp;
                                if (asprintf(&pattern, "*%.*s", (int)(chptr - endtag), endtag) < 0) {
                                    message_OOM();
                                    RAISE_ERROR();
                                }

                                tabooPatterns[tabooCount] = pattern;
                                tabooCount++;
                            }

                            endtag = chptr;
                            if (*endtag == ',')
                                endtag++;
                            while (*endtag && isspace((unsigned char)*endtag))
                                endtag++;
                        }
                    } else if (!strcmp(key, "taboopat")) {
                        char *endtag;

                        if (newlog != defConfig) {
                            message(MESS_ERROR,
                                    "%s:%d taboopat may not appear inside "
                                    "of log file definition\n", configFile,
                                    lineNum);
                            state = STATE_ERROR;
                            continue;
                        }
                        free(key);
                        key = isolateValue(configFile, lineNum, "taboopat", &start,
                                           &buf, length);
                        if (key == NULL)
                            continue;

                        endtag = key;
                        if (*endtag == '+') {
                            endtag++;
                            while (isspace((unsigned char)*endtag) && *endtag)
                                endtag++;
                        } else {
                            free_2d_array(tabooPatterns, tabooCount);
                            tabooCount = 0;
                            /* realloc of NULL is safe by definition */
                            tabooPatterns = NULL;
                        }

                        while (*endtag) {
                            char *pattern = NULL;
                            char **tmp;

                            chptr = endtag;
                            while (!isspace((unsigned char)*chptr) && *chptr != ',' && *chptr)
                                chptr++;

                            tmp = realloc(tabooPatterns, sizeof(*tabooPatterns) *
                                    (tabooCount + 1));
                            if (tmp == NULL) {
                                message_OOM();
                                RAISE_ERROR();
                            }
                            tabooPatterns = tmp;
                            if (asprintf(&pattern, "%.*s", (int)(chptr - endtag), endtag) < 0) {
                                message_OOM();
                                RAISE_ERROR();
                            }

                            tabooPatterns[tabooCount] = pattern;
                            tabooCount++;

                            endtag = chptr;
                            if (*endtag == ',')
                                endtag++;
                            while (*endtag && isspace((unsigned char)*endtag))
                                endtag++;
                        }
                    } else if (!strcmp(key, "include")) {
                        int rv;

                        free(key);
                        key = isolateValue(configFile, lineNum, "include", &start,
                                           &buf, length);
                        if (key == NULL) {
                            RAISE_ERROR();
                        }

                        if (key[0] == '~' && key[1] == '/') {
                            /* replace '~' with content of $HOME cause low-level functions
                             * like stat() do not support the glob ~
                             */
                            const char *env_home = secure_getenv("HOME");
                            char *new_key = NULL;

                            if (!env_home) {
                                const struct passwd *pwd = getpwuid(getuid());
                                message(MESS_DEBUG,
                                        "%s:%d cannot get HOME directory from environment "
                                        "to replace ~/ in include directive\n",
                                        configFile, lineNum);
                                if (!pwd) {
                                    message(MESS_ERROR, "%s:%d cannot get passwd entry for "
                                            "running user %u: %s\n",
                                           configFile, lineNum, getuid(), strerror(errno));
                                    RAISE_ERROR();
                                }
                                env_home = pwd->pw_dir;
                            }

                            if (asprintf(&new_key, "%s/%s", env_home, key + 2) < 0) {
                                message_OOM();
                                RAISE_ERROR();
                            }
                            message(MESS_DEBUG, "%s:%d replaced %s with '%s' for include directive\n",
                                    configFile, lineNum, key, env_home);
                            free(key);
                            key = new_key;
                        }

                        message(MESS_DEBUG, "including %s\n", key);
                        if (recursion_depth >= MAX_NESTING) {
                            message(MESS_ERROR, "%s:%d include nesting too deep\n",
                                    configFile, lineNum);
                            logerror = 1;
                            continue;
                        }

                        ++recursion_depth;
                        rv = readConfigPath(key, newlog);
                        --recursion_depth;

                        if (rv) {
                            logerror = 1;
                            continue;
                        }
                    } else if (!strcmp(key, "olddir")) {
                        freeLogItem (oldDir);

                        if (!(newlog->oldDir = readPath(configFile, lineNum,
                                        "olddir", &start, &buf, length))) {
                            RAISE_ERROR();
                        }
                        message(MESS_DEBUG, "olddir is now %s\n", newlog->oldDir);
                    } else if (!strcmp(key, "extension")) {
                        free(key);
                        key = isolateValue(configFile, lineNum, "extension name", &start,
                                           &buf, length);
                        if (key == NULL) {
                            RAISE_ERROR();
                        }
                        freeLogItem (extension);
                        newlog->extension = key;
                        key = NULL;
                        message(MESS_DEBUG, "extension is now %s\n", newlog->extension);

                    } else if (!strcmp(key, "addextension")) {
                        free(key);
                        key = isolateValue(configFile, lineNum, "addextension name", &start,
                                           &buf, length);
                        if (key == NULL) {
                            RAISE_ERROR();
                        }
                        freeLogItem (addextension);
                        newlog->addextension = key;
                        key = NULL;
                        message(MESS_DEBUG, "addextension is now %s\n",
                                newlog->addextension);

                    } else if (!strcmp(key, "compresscmd")) {
                        char *compresscmd_full;
                        const char *compresscmd_base;
                        unsigned i;

                        freeLogItem (compress_prog);

                        if (!
                                (newlog->compress_prog =
                                 readPath(configFile, lineNum, "compress", &start, &buf, length))) {
                            RAISE_ERROR();
                        }

                        message(MESS_DEBUG, "compress_prog is now %s\n",
                                newlog->compress_prog);

                        compresscmd_full = strdup(newlog->compress_prog);
                        if (compresscmd_full == NULL) {
                            message_OOM();
                            RAISE_ERROR();
                        }

                        compresscmd_base = basename(compresscmd_full);

                        /* we check whether we changed the compress_cmd. In case we use the appropriate extension
                           as listed in compress_cmd_list */
                        for(i = 0; i < compress_cmd_list_size; i++) {
                            if (!strcmp(compress_cmd_list[i].cmd, compresscmd_base)) {
                                freeLogItem (compress_ext);
                                newlog->compress_ext = strdup(compress_cmd_list[i].ext);
                                if (newlog->compress_ext == NULL) {
                                    message_OOM();
                                    free(compresscmd_full);
                                    RAISE_ERROR();
                                }
                                message(MESS_DEBUG, "compress_ext was changed to %s\n", newlog->compress_ext);
                                break;
                            }
                        }
                        free(compresscmd_full);
                    } else if (!strcmp(key, "uncompresscmd")) {
                        freeLogItem (uncompress_prog);

                        if (!
                                (newlog->uncompress_prog =
                                 readPath(configFile, lineNum, "uncompress",
                                          &start, &buf, length))) {
                            RAISE_ERROR();
                        }

                        message(MESS_DEBUG, "uncompress_prog is now %s\n",
                                newlog->uncompress_prog);

                    } else if (!strcmp(key, "compressoptions")) {
                        char *options;

                        if (newlog->compress_options_list) {
                            free(newlog->compress_options_list);
                            newlog->compress_options_list = NULL;
                            newlog->compress_options_count = 0;
                        }

                        if (!(options = isolateLine(&start, &buf, length))) {
                            RAISE_ERROR();
                        }

                        if (poptParseArgvString(options,
                                                &newlog->compress_options_count,
                                                &newlog->compress_options_list)) {
                            message(MESS_ERROR,
                                    "%s:%d invalid compression options\n",
                                    configFile, lineNum);
                            free(options);
                            RAISE_ERROR();
                        }

                        message(MESS_DEBUG, "compress_options is now %s\n",
                                options);
                        free(options);
                    } else if (!strcmp(key, "compressext")) {
                        freeLogItem (compress_ext);

                        if (!
                                (newlog->compress_ext =
                                 readPath(configFile, lineNum, "compress-ext",
                                          &start, &buf, length))) {
                            RAISE_ERROR();
                        }

                        message(MESS_DEBUG, "compress_ext is now %s\n",
                                newlog->compress_ext);
                    } else {
                        message(MESS_ERROR, "%s:%d unknown option '%s' "
                                "-- ignoring line\n", configFile, lineNum, key);
                        if (*start != '\n')
                            state = STATE_SKIP_LINE;
                    }
                } else if (*start == '/' || *start == '"' || *start == '\''
#ifdef GLOB_TILDE
                        || *start == '~'
#endif
                        ) {
                    char *glob_string;
                    size_t glob_count;
                    int argc, argNum;
                    const char **argv;
                    in_config = 0;
                    if (newlog != defConfig) {
                        message(MESS_ERROR, "%s:%d unexpected log filename\n",
                                configFile, lineNum);
                        state = STATE_ERROR;
                        continue;
                    }

                    /* If no compression options were found in config file, set
                       default values */
                    if (!newlog->compress_prog)
                        newlog->compress_prog = strdup(COMPRESS_COMMAND);
                    if (!newlog->uncompress_prog)
                        newlog->uncompress_prog = strdup(UNCOMPRESS_COMMAND);
                    if (!newlog->compress_ext)
                        newlog->compress_ext = strdup(COMPRESS_EXT);

                    if (!newlog->compress_prog || !newlog->uncompress_prog || !newlog->compress_ext) {
                        message_OOM();
                        goto error;
                    }

                    /* Allocate a new logInfo structure and insert it into the logs
                       queue, copying the actual values from defConfig */
                    if ((newlog = newLogInfo(defConfig)) == NULL)
                        goto error;

                    glob_string = parseGlobString(configFile, lineNum, buf, length, &start);
                    if (glob_string)
                        in_config = 1;
                    else
                        /* error already printed */
                        goto error;

                    if (poptParseArgvString(glob_string, &argc, &argv)) {
                        message(MESS_ERROR, "%s:%d error parsing filename\n",
                                configFile, lineNum);
                        free(glob_string);
                        goto error;
                    } else if (argc < 1) {
                        message(MESS_ERROR,
                                "%s:%d { expected after log file name(s)\n",
                                configFile, lineNum);
                        free(glob_string);
                        goto error;
                    }

                    newlog->files = NULL;
                    newlog->numFiles = 0;
                    for (argNum = 0; argNum < argc; argNum++) {
                        char **tmp;
                        int rc;
                        glob_t globResult;

                        if (globerr_msg) {
                            free(globerr_msg);
                            globerr_msg = NULL;
                        }

                        rc = glob(argv[argNum], GLOB_NOCHECK
#ifdef GLOB_TILDE
                                | GLOB_TILDE
#endif
                                , globerr, &globResult);
                        if (rc == GLOB_ABORTED) {
                            if (newlog->flags & LOG_FLAG_MISSINGOK) {
                                continue;
                            }

                            /* We don't yet know whether this stanza has "missingok"
                             * set, so store the error message for later. */
                            rc = asprintf(&globerr_msg, "%s:%d glob failed for %s: %s\n",
                                          configFile, lineNum, argv[argNum], strerror(glob_errno));
                            if (rc == -1)
                                globerr_msg = NULL;

                            globResult.gl_pathc = 0;
                        }

                        tmp = realloc(newlog->files,
                                    sizeof(*newlog->files) * (newlog->numFiles +
                                        globResult.
                                        gl_pathc));
                        if (tmp == NULL) {
                            message_OOM();
                            logerror = 1;
                            goto duperror;
                        }

                        newlog->files = tmp;

                        for (glob_count = 0; glob_count < globResult.gl_pathc; glob_count++) {
                            struct logInfo *log;

                            /* if we glob directories we can get false matches */
                            if (!lstat(globResult.gl_pathv[glob_count], &sb) &&
                                    S_ISDIR(sb.st_mode)) {
                                continue;
                            }

                            for (log = logs.tqh_first; log != NULL;
                                    log = log->list.tqe_next) {
                                unsigned k;
                                for (k = 0; k < log->numFiles; k++) {
                                    if (!strcmp(log->files[k],
                                                globResult.gl_pathv[glob_count])) {
                                        message(MESS_ERROR,
                                                "%s:%d duplicate log entry for %s\n",
                                                configFile, lineNum,
                                                globResult.gl_pathv[glob_count]);
                                        logerror = 1;
                                        goto duperror;
                                    }
                                }
                            }

                            newlog->files[newlog->numFiles] =
                                strdup(globResult.gl_pathv[glob_count]);
                            if (newlog->files[newlog->numFiles] == NULL) {
                                message_OOM();
                                logerror = 1;
                                goto duperror;
                            }
                            newlog->numFiles++;
                        }
duperror:
                        globfree(&globResult);
                    }

                    newlog->pattern = glob_string;

                    free(argv);

                } else if (*start == '}') {
                    if (newlog == defConfig) {
                        message(MESS_ERROR, "%s:%d unexpected }\n", configFile,
                                lineNum);
                        goto error;
                    }
                    if (!in_config) {
                        message(MESS_ERROR, "%s:%d unexpected } (missing previous '{')\n", configFile,
                                lineNum);
                        goto error;
                    }
                    in_config = 0;
                    if (globerr_msg) {
                        if (!(newlog->flags & LOG_FLAG_MISSINGOK))
                            message(MESS_ERROR, "%s", globerr_msg);
                        free(globerr_msg);
                        globerr_msg = NULL;
                        if (!(newlog->flags & LOG_FLAG_MISSINGOK))
                            goto error;
                    }

                    if (newlog->oldDir) {
                        unsigned j;
                        for (j = 0; j < newlog->numFiles; j++) {
                            char *ld;
                            char *dirpath;
                            const char *dirName;
                            struct stat sb2;

                            dirpath = strdup(newlog->files[j]);
                            if (dirpath == NULL) {
                                message_OOM();
                                goto error;
                            }

                            dirName = dirname(dirpath);
                            if (stat(dirName, &sb2)) {
                                if (!(newlog->flags & LOG_FLAG_MISSINGOK)) {
                                    message(MESS_ERROR,
                                            "%s:%d error verifying log file "
                                            "path %s: %s\n", configFile, lineNum,
                                            dirName, strerror(errno));
                                    free(dirpath);
                                    goto error;
                                }
                                else {
                                    message(MESS_DEBUG,
                                            "%s:%d verifying log file "
                                            "path failed %s: %s, log is probably missing, "
                                            "but missingok is set, so this is not an error.\n",
                                            configFile, lineNum,
                                            dirName, strerror(errno));
                                    free(dirpath);
                                    continue;
                                }
                            }
                            if (asprintf(&ld, "%s/%s", dirName, newlog->oldDir) < 0) {
                                message_OOM();
                                free(dirpath);
                                goto error;
                            }

                            free(dirpath);

                            if (newlog->oldDir[0] != '/') {
                                dirName = ld;
                            }
                            else {
                                dirName = newlog->oldDir;
                            }

                            if (stat(dirName, &sb)) {
                                if (errno == ENOENT && (newlog->flags & LOG_FLAG_OLDDIRCREATE)) {
                                    int ret;
                                    if (newlog->flags & LOG_FLAG_SU) {
                                        if (switch_user(newlog->suUid, newlog->suGid) != 0) {
                                            free(ld);
                                            goto error;
                                        }
                                    }
                                    ret = mkpath(dirName, newlog->olddirMode,
                                            newlog->olddirUid, newlog->olddirGid);
                                    if (newlog->flags & LOG_FLAG_SU) {
                                        if (switch_user_back() != 0) {
                                            free(ld);
                                            goto error;
                                        }
                                    }
                                    if (ret) {
                                        free(ld);
                                        goto error;
                                    }
                                }
                                else {
                                    message(MESS_ERROR, "%s:%d error verifying olddir "
                                            "path %s: %s\n", configFile, lineNum,
                                            dirName, strerror(errno));
                                    free(ld);
                                    goto error;
                                }
                            }

                            free(ld);

                            if (sb.st_dev != sb2.st_dev
                                    && !(newlog->flags & (LOG_FLAG_COPYTRUNCATE | LOG_FLAG_COPY | LOG_FLAG_TMPFILENAME))) {
                                message(MESS_ERROR,
                                        "%s:%d olddir %s and log file %s "
                                        "are on different devices\n", configFile,
                                        lineNum, newlog->oldDir, newlog->files[j]);
                                goto error;
                            }
                        }
                    }

                    criterium_set = 0;
                    newlog = defConfig;
                    state = STATE_DEFINITION_END;
                } else if (*start != '\n') {
                    message(MESS_ERROR, "%s:%d lines must begin with a keyword "
                            "or a filename (possibly in double quotes)\n",
                            configFile, lineNum);
                    state = STATE_SKIP_LINE;
                }
                break;
            case STATE_SKIP_LINE:
            case STATE_SKIP_LINE | STATE_SKIP_CONFIG:
                if (*start == '\n')
                    state = (state & STATE_SKIP_CONFIG) ? STATE_SKIP_CONFIG : STATE_DEFAULT;
                break;
            case STATE_SKIP_LINE | STATE_LOAD_SCRIPT:
                if (*start == '\n')
                    state = STATE_LOAD_SCRIPT;
                break;
            case STATE_SKIP_LINE | STATE_LOAD_SCRIPT | STATE_SKIP_CONFIG:
                if (*start == '\n')
                    state = STATE_LOAD_SCRIPT | STATE_SKIP_CONFIG;
                break;
            case STATE_DEFINITION_END:
            case STATE_DEFINITION_END | STATE_SKIP_CONFIG:
                if (isblank((unsigned char)*start))
                    continue;
                if (*start != '\n') {
                    message(MESS_ERROR, "%s:%d, unexpected text after }\n",
                            configFile, lineNum);
                    state = STATE_SKIP_LINE | ((state & STATE_SKIP_CONFIG) ? STATE_SKIP_CONFIG : 0);
                }
                else
                    state = (state & STATE_SKIP_CONFIG) ? STATE_SKIP_CONFIG : STATE_DEFAULT;
                break;
            case STATE_ERROR:
                assert(newlog != defConfig);

                message(MESS_ERROR, "found error in %s, skipping\n",
                        newlog->pattern ? newlog->pattern : "log config");

                logerror = 1;
                state = STATE_SKIP_CONFIG;
                break;
            case STATE_LOAD_SCRIPT:
            case STATE_LOAD_SCRIPT | STATE_SKIP_CONFIG:
                free(key);
                key = isolateWord(&start, &buf, length);
                if (key == NULL)
                    continue;

                if (strcmp(key, "endscript") == 0) {
                    if (state & STATE_SKIP_CONFIG) {
                        state = STATE_SKIP_CONFIG;
                    }
                    else {
                        const char *endtag = start - 9;
                        while (*endtag != '\n')
                            endtag--;
                        endtag++;
                        *scriptDest = strndup(scriptStart, (size_t)(endtag - scriptStart));
                        if (*scriptDest == NULL) {
                            message_OOM();
                            goto error;
                        }

                        scriptDest = NULL;
                        scriptStart = NULL;
                    }
                    state = (state & STATE_SKIP_CONFIG) ? STATE_SKIP_CONFIG : STATE_DEFAULT;
                }
                else {
                    state = (*start == '\n' ? 0 : STATE_SKIP_LINE) |
                        STATE_LOAD_SCRIPT |
                        ((state & STATE_SKIP_CONFIG) ? STATE_SKIP_CONFIG : 0);
                }
                break;
            case STATE_SKIP_CONFIG:
                if (*start == '}') {
                    state = STATE_DEFAULT;
                    freeTailLogs(1);
                    newlog = defConfig;
                }
                else {
                    free(key);
                    key = isolateWord(&start, &buf, length);
                    if (key == NULL)
                        continue;
                    if (
                            (strcmp(key, "postrotate") == 0) ||
                            (strcmp(key, "prerotate") == 0) ||
                            (strcmp(key, "firstaction") == 0) ||
                            (strcmp(key, "lastaction") == 0) ||
                            (strcmp(key, "preremove") == 0)
                            ) {
                        state = STATE_LOAD_SCRIPT | STATE_SKIP_CONFIG;
                    }
                    else {
                        /* isolateWord moves the "start" pointer.
                         * If we have a line like
                         *    rotate 5
                         * after isolateWord "start" points to "5" and it
                         * is OK to skip the line, but if we have a line
                         * like the following
                         *    nocompress
                         * after isolateWord "start" points to "\n". In
                         * this case if we skip a line, we skip the next
                         * line, not the current "nocompress" one,
                         * because in the for cycle the "start"
                         * pointer is increased by one and, after this,
                         * "start" points to the beginning of the next line.
                         */
                        if (*start != '\n') {
                            state = STATE_SKIP_LINE | STATE_SKIP_CONFIG;
                        }
                    }
                }
                break;
            default:
                message(MESS_FATAL,
                        "%s: %d: readConfigFile() unknown state: %#x\n",
                        configFile, lineNum, state);
        }
        if (*start == '\n') {
            lineNum++;
        }

next_state: ;
    }

    if (scriptStart) {
        message(MESS_ERROR,
                "%s:prerotate, postrotate or preremove without endscript\n",
                configFile);
        goto error;
    }

    free(key);

    munmap(buf, length);
    close(fd);
    free(globerr_msg);
    return logerror;
error:
    /* free is a NULL-safe operation */
    free(key);
    munmap(buf, length);
    close(fd);
    free(globerr_msg);
    return 1;
}

/* vim: set et sw=4 ts=4: */
