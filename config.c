#include "queue.h"
/* Alloca is defined in stdlib.h in NetBSD */
#ifndef __NetBSD__
#include <alloca.h>
#endif
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
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <wchar.h>
#include <wctype.h>
#include <fnmatch.h>
#include <sys/mman.h>
#include <libgen.h>

#include "log.h"
#include "logrotate.h"

#if !defined(GLOB_ABORTED) && defined(GLOB_ABEND)
#define GLOB_ABORTED GLOB_ABEND
#endif

#define REALLOC_STEP    10

#if defined(SunOS)
#include <limits.h>
#if !defined(isblank)
#define isblank(c) 	( (c) == ' ' || (c) == '\t' ) ? 1 : 0
#endif
#endif

#ifdef __hpux
#include "asprintf.c"
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
	size++;
	va_start(arg, format);
	str = malloc(size);
	if (str == NULL) {
		va_end(arg);
		/*
		 * Strictly speaking, GNU asprintf doesn't do this,
		 * but the caller isn't checking the return value.
		 */
		fprintf(stderr, "failed to allocate memory\\n");
		exit(1);
	}
	rv = vsnprintf(str, size, format, arg);
	va_end(arg);

	*string_ptr = str;
	return (rv);
}

#endif

#if !defined(HAVE_STRNDUP)
char *strndup(const char *s, size_t n)
{
       size_t nAvail;
       char *p;

       if(!s)
               return NULL;

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
    {"compress", ".Z"},
    {"zip", "zip"},
};
static const int compress_cmd_list_size = sizeof(compress_cmd_list)
    / sizeof(compress_cmd_list[0]);

enum {
	STATE_DEFAULT = 2,
	STATE_SKIP_LINE = 4,
	STATE_DEFINITION_END = 8,
	STATE_SKIP_CONFIG = 16,
	STATE_LOAD_SCRIPT = 32,
	STATE_ERROR = 64,
};

static const char *defTabooExts[] = { ".rpmsave", ".rpmorig", "~", ",v",
    ".disabled", ".dpkg-old", ".dpkg-dist", ".dpkg-new", ".cfsaved",
    ".ucf-old", ".ucf-dist", ".ucf-new",
    ".rpmnew", ".swp", ".cfsaved", ".rhn-cfg-tmp-*"
};
static int defTabooCount = sizeof(defTabooExts) / sizeof(char *);

/* I shouldn't use globals here :-( */
static char **tabooPatterns = NULL;
int tabooCount = 0;
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
	key = strndup(start, endtag - start + 1);
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
	key = strndup(start, endtag - start);
	*strt = endtag;
	return key;
}

static char *readPath(const char *configFile, int lineNum, const char *key,
		      char **startPtr, char **buf, size_t length)
{
    char *chptr;
    char *start = *startPtr;
    char *path;

    wchar_t pwc;
    size_t len;

    if ((start = isolateValue(configFile, lineNum, key, startPtr, buf, length)) != NULL) {

	chptr = start;

	while( (len = mbrtowc(&pwc, chptr, strlen(chptr), NULL)) != 0 && strlen(chptr) != 0) {
		if( len == (size_t)(-1) || len == (size_t)(-2) || !iswprint(pwc) || iswblank(pwc) ) {
		    message(MESS_ERROR, "%s:%d bad %s path %s\n",
			    configFile, lineNum, key, start);
		    return NULL;
		}
		chptr += len;
	}

/*
	while (*chptr && isprint((unsigned char)*chptr) && *chptr != ' ')
	    chptr++;
	if (*chptr) {
	    message(MESS_ERROR, "%s:%d bad %s path %s\n",
		    configFile, lineNum, key, start);
	    return NULL;
	}
*/

	path = strdup(start);
	free(start);

	return path;
    } else
	return NULL;
}

static int readModeUidGid(const char *configFile, int lineNum, char *key,
							const char *directive, mode_t *mode, uid_t *uid,
							gid_t *gid) {
	char u[200], g[200];
	unsigned int m;
	char tmp;
	int rc;
	struct group *group;
	struct passwd *pw = NULL;

	if (!strcmp("su", directive))
	    /* do not read <mode> for the 'su' directive */
	    rc = 0;
	else
	    rc = sscanf(key, "%o %199s %199s%c", &m, u, g, &tmp);

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
		pw = getpwnam(u);
		if (!pw) {
			message(MESS_ERROR, "%s:%d unknown user '%s'\n",
				configFile, lineNum, u);
			return -1;
		}
		*uid = pw->pw_uid;
		endpwent();
	}
	if (rc > 2) {
		group = getgrnam(g);
		if (!group) {
			message(MESS_ERROR, "%s:%d unknown group '%s'\n",
				configFile, lineNum, g);
			return -1;
		}
		*gid = group->gr_gid;
		endgrent();
	}

	return 0;
}

static char *readAddress(const char *configFile, int lineNum, const char *key,
			 char **startPtr, char **buf, size_t length)
{
    char *endtag, *chptr;
    char *start = *startPtr;
    char *address;
	
    if ((endtag = isolateValue(configFile, lineNum, key, startPtr, buf, length)) != NULL) {

	chptr = endtag;
	while (*chptr && isprint((unsigned char)*chptr) && *chptr != ' ') {
	    chptr++;
	}

	if (*chptr) {
	    message(MESS_ERROR, "%s:%d bad %s address %s\n",
		    configFile, lineNum, key, start);
	    return NULL;
	}

	address = strdup(endtag);
	
	free(endtag);

	return address;
    } else
	return NULL;
}

static int do_mkdir(const char *path, mode_t mode, uid_t uid, gid_t gid) {
	struct stat sb;

	if (stat(path, &sb) != 0) {
		if (mkdir(path, mode) != 0 && errno != EEXIST) {
			message(MESS_ERROR, "error creating %s: %s\n",
				path, strerror(errno));
			return -1;
		}
		if (chown(path, uid, gid) != 0) {
			message(MESS_ERROR, "error setting owner of %s to uid %d and gid %d: %s\n",
				path, uid, gid, strerror(errno));
			return -1;
		}
		if (chmod(path, mode) != 0) {
			message(MESS_ERROR, "error setting permissions of %s to 0%o: %s\n",
				path, mode, strerror(errno));
			return -1;
		}
	}
	else if (!S_ISDIR(sb.st_mode)) {
		message(MESS_ERROR, "path %s already exists, but it is not a directory\n",
			path);
		errno = ENOTDIR;
		return -1;
	}

	return 0;
}

static int mkpath(const char *path, mode_t mode, uid_t uid, gid_t gid) {
	char *pp;
	char *sp;
	int rv;
	char *copypath = strdup(path);

	rv = 0;
	pp = copypath;
	while (rv == 0 && (sp = strchr(pp, '/')) != 0) {
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
	int i;

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
    return strcoll(*((char **) p), *((char **) q));
}

/* Free memory blocks pointed to by pointers in a 2d array and the array itself */
static void free_2d_array(char **array, int lines_count)
{
    int i;
    for (i = 0; i < lines_count; ++i)
	free(array[i]);
    free(array);
}

static void copyLogInfo(struct logInfo *to, struct logInfo *from)
{
    memset(to, 0, sizeof(*to));
    if (from->oldDir)
	to->oldDir = strdup(from->oldDir);
    to->criterium = from->criterium;
    to->weekday = from->weekday;
    to->threshhold = from->threshhold;
    to->minsize = from->minsize;
    to->maxsize = from->maxsize;
    to->rotateCount = from->rotateCount;
    to->rotateMinAge = from->rotateMinAge;
    to->rotateAge = from->rotateAge;
    to->logStart = from->logStart;
    if (from->pre)
	to->pre = strdup(from->pre);
    if (from->post)
	to->post = strdup(from->post);
    if (from->first)
	to->first = strdup(from->first);
    if (from->last)
	to->last = strdup(from->last);
    if (from->preremove)
	to->preremove = strdup(from->preremove);
    if (from->logAddress)
	to->logAddress = strdup(from->logAddress);
    if (from->extension)
	to->extension = strdup(from->extension);
    if (from->compress_prog)
	to->compress_prog = strdup(from->compress_prog);
    if (from->uncompress_prog)
	to->uncompress_prog = strdup(from->uncompress_prog);
    if (from->compress_ext)
	to->compress_ext = strdup(from->compress_ext);
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
    }
	if (from->dateformat)
		to->dateformat = strdup(from->dateformat);
}

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

static struct logInfo *newLogInfo(struct logInfo *template)
{
	struct logInfo *new;

	if ((new = malloc(sizeof(*new))) == NULL)
		return NULL;

	copyLogInfo(new, template);
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
	numLogs--;
}

static void freeTailLogs(int num)
{
	message(MESS_DEBUG, "removing last %d log configs\n", num);

	while (num--)
		removeLogInfo(TAILQ_LAST(&logs, logInfoHead));

}

static int readConfigPath(const char *path, struct logInfo *defConfig)
{
    struct stat sb;
    int here, result = 0;
    struct logInfo defConfigBackup;

    if (stat(path, &sb)) {
	message(MESS_ERROR, "cannot stat %s: %s\n", path, strerror(errno));
	return 1;
    }

    if (S_ISDIR(sb.st_mode)) {
	char **namelist, **p;
	struct dirent *dp;
	int files_count, i;
	DIR *dirp;

	here = open(".", O_RDONLY);

	if ((dirp = opendir(path)) == NULL) {
	    message(MESS_ERROR, "cannot open directory %s: %s\n", path,
		    strerror(errno));
	    close(here);
	    return 1;
	}
	files_count = 0;
	namelist = NULL;
	while ((dp = readdir(dirp)) != NULL) {
	    if (checkFile(dp->d_name)) {
		/* Realloc memory for namelist array if necessary */
		if (files_count % REALLOC_STEP == 0) {
		    p = (char **) realloc(namelist,
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
			message(MESS_ERROR, "cannot realloc: %s\n",
				strerror(errno));
			return 1;
		    }
		}
		/* Alloc memory for file name */
		if ((namelist[files_count] =
		     (char *) malloc(strlen(dp->d_name) + 1))) {
		    strcpy(namelist[files_count], dp->d_name);
		    files_count++;
		} else {
		    free_2d_array(namelist, files_count);
		    closedir(dirp);
		    close(here);
		    message(MESS_ERROR, "cannot realloc: %s\n",
			    strerror(errno));
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
	    copyLogInfo(&defConfigBackup, defConfig);
	    if (readConfigFile(namelist[i], defConfig)) {
		message(MESS_ERROR, "found error in file %s, skipping\n", namelist[i]);
		freeLogInfo(defConfig);
		copyLogInfo(defConfig, &defConfigBackup);
		freeLogInfo(&defConfigBackup);
		result = 1;
		continue;
	    }
	    freeLogInfo(&defConfigBackup);
	}

	if (fchdir(here) < 0) {
		message(MESS_ERROR, "could not change directory to '.'");
	}
	close(here);
	free_2d_array(namelist, files_count);
    } else {
	copyLogInfo(&defConfigBackup, defConfig);
	if (readConfigFile(path, defConfig)) {
	    freeLogInfo(defConfig);
	    copyLogInfo(defConfig, &defConfigBackup);
	    result = 1;
	}
	freeLogInfo(&defConfigBackup);
    }

    return result;
}

int readAllConfigPaths(const char **paths)
{
    int i, result = 0;
    const char **file;
    struct logInfo defConfig = {
		.pattern = NULL,
		.files = NULL,
		.numFiles = 0,
		.oldDir = NULL,
		.criterium = ROT_SIZE,
		.threshhold = 1024 * 1024,
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
    for (i = 0; i < defTabooCount; i++) {
	int bytes;
	char *pattern = NULL;

	/* generate a pattern by concatenating star (wildcard) to the
	 * suffix literal
	 */
	bytes = asprintf(&pattern, "*%s", defTabooExts[i]);
	if (bytes != -1) {
	    tabooPatterns[i] = pattern;
	    tabooCount++;
	} else {
	    free_2d_array(tabooPatterns, tabooCount);
	    message(MESS_ERROR, "cannot malloc: %s\n", strerror(errno));
	    return 1;
	}
    }

    for (file = paths; *file; file++) {
	if (readConfigPath(*file, &defConfig))
	    result = 1;
    }
    free_2d_array(tabooPatterns, tabooCount);
    freeLogInfo(&defConfig);
    return result;
}

static int globerr(const char *pathname, int theerr)
{
    (void) pathname;

    /* A missing directory is not an error, so return 0 */
    if (theerr == ENOTDIR)
        return 0;

    glob_errno = theerr;

    /* We want the glob operation to abort on error, so return 1 */
    return 1;
}

#define freeLogItem(what) \
	do { \
		free(newlog->what); \
		newlog->what = NULL; \
	} while (0);
#define RAISE_ERROR() \
	if (newlog != defConfig) { \
		state = STATE_ERROR; \
		continue; \
	} else { \
		goto error; \
	}
#define MAX_NESTING 16U

static int readConfigFile(const char *configFile, struct logInfo *defConfig)
{
    int fd;
    char *buf, *endtag, *key = NULL;
    off_t length;
    int lineNum = 1;
    unsigned long long multiplier;
    int i, k;
    char *scriptStart = NULL;
    char **scriptDest = NULL;
    struct logInfo *newlog = defConfig;
    char *start, *chptr;
    char *dirName;
    struct passwd *pw = NULL;
    int rc;
    struct stat sb, sb2;
    glob_t globResult;
    const char **argv;
    int argc, argNum;
	int flags;
	int state = STATE_DEFAULT;
    int logerror = 0;
    struct logInfo *log;
	static unsigned recursion_depth = 0U;
	char *globerr_msg = NULL;
	int in_config = 0;
	int rv;
	struct flock fd_lock = {
		.l_start = 0,
		.l_len = 0,
		.l_whence = SEEK_SET,
		.l_type = F_RDLCK
	};

    /* FIXME: createOwner and createGroup probably shouldn't be fixed
       length arrays -- of course, if we aren't run setuid it doesn't
       matter much */

	fd = open(configFile, O_RDONLY);
	if (fd < 0) {
		message(MESS_ERROR, "failed to open config file %s: %s\n",
			configFile, strerror(errno));
		return 1;
	}
	if ((flags = fcntl(fd, F_GETFD)) == -1) {
		message(MESS_ERROR, "Could not retrieve flags from file %s\n",
				configFile);
		close(fd);
		return 1;
	}
	flags |= FD_CLOEXEC;
	if (fcntl(fd, F_SETFD, flags) == -1) {
		message(MESS_ERROR, "Could not set flags on file %s\n",
				configFile);
		close(fd);
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
    
	if (!(pw = getpwuid(getuid()))) {
		message(MESS_ERROR, "Logrotate UID is not in passwd file.\n");
		close(fd);
		return 1;
	}

	if (getuid() == ROOT_UID) {
		if ((sb.st_mode & 07533) != 0400) {
			message(MESS_ERROR,
				"Ignoring %s because of bad file mode - must be 0644 or 0444.\n",
				configFile);
			close(fd);
			return 0;
		}

		if ((pw = getpwuid(ROOT_UID)) == NULL) {
			message(MESS_DEBUG,
				"Ignoring %s because there's no password entry for the owner.\n",
				configFile);
			close(fd);
			return 0;
		}

		if (sb.st_uid != ROOT_UID && (pw == NULL ||
				sb.st_uid != pw->pw_uid ||
				pw->pw_uid != ROOT_UID)) {
			message(MESS_DEBUG,
				"Ignoring %s because the file owner is wrong (should be root or user with uid 0).\n",
				configFile);
			close(fd);
			return 0;
		}
	}

	length = sb.st_size;

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
 	buf = mmap(NULL, (size_t) length, PROT_READ,
 			MAP_PRIVATE | MAP_POPULATE, fd, (off_t) 0);
#else /* MAP_POPULATE */
	buf = mmap(NULL, (size_t) length, PROT_READ,
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
	madvise(buf, (size_t)(length + 2),
			MADV_SEQUENTIAL | MADV_WILLNEED | MADV_DONTFORK);
#else /* MADV_DONTFORK */
	madvise(buf, (size_t)(length + 2),
			MADV_SEQUENTIAL | MADV_WILLNEED);
#endif /* MADV_DONTFORK */
#endif /* HAVE_MADVISE */

    message(MESS_DEBUG, "reading config file %s\n", configFile);

	start = buf;
    for (start = buf; start - buf < length; start++) {
	if (key) {
		free(key);
		key = NULL;
	}
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
				if ((key = isolateWord(&start, &buf, length)) == NULL)
					continue;
				if (!strcmp(key, "compress")) {
					newlog->flags |= LOG_FLAG_COMPRESS;
				} else if (!strcmp(key, "nocompress")) {
					newlog->flags &= ~LOG_FLAG_COMPRESS;
				} else if (!strcmp(key, "compress")) {
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
				} else if (!strcmp(key, "nocopytruncate")) {
					newlog->flags &= ~LOG_FLAG_COPYTRUNCATE;
				} else if (!strcmp(key, "renamecopy")) {
					newlog->flags |= LOG_FLAG_TMPFILENAME;
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
				} else if (!strcmp(key, "dateformat")) {
					freeLogItem(dateformat);
					newlog->dateformat = isolateLine(&start, &buf, length);
					if (newlog->dateformat == NULL)
						continue;
				} else if (!strcmp(key, "noolddir")) {
					newlog->oldDir = NULL;
				} else if (!strcmp(key, "mailfirst")) {
					newlog->flags |= LOG_FLAG_MAILFIRST;
				} else if (!strcmp(key, "maillast")) {
					newlog->flags &= ~LOG_FLAG_MAILFIRST;
				} else if (!strcmp(key, "su")) {
					mode_t tmp_mode = NO_MODE;
					free(key);
					key = isolateLine(&start, &buf, length);
					if (key == NULL)
						continue;

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

					newlog->flags |= LOG_FLAG_SU;
				} else if (!strcmp(key, "create")) {
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
					unsigned long long size = 0;
					char *opt = key;

					key = isolateValue(configFile, lineNum, opt, &start, &buf, length);
					if (key && key[0]) {
						int l = strlen(key) - 1;
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

						size = multiplier * strtoull(key, &chptr, 0);
						if (*chptr) {
							message(MESS_ERROR, "%s:%d bad size '%s'\n",
								configFile, lineNum, key);
							free(opt);
							RAISE_ERROR();
						}
						if (!strncmp(opt, "size", 4)) {
						  newlog->criterium = ROT_SIZE;
						  newlog->threshhold = size;
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
					if ((key = isolateValue(configFile, lineNum, "shred cycles", 
							&start, &buf, length)) != NULL) {
						newlog->shred_cycles = strtoul(key, &chptr, 0);
						if (*chptr || newlog->shred_cycles < 0) {
							message(MESS_ERROR, "%s:%d bad shred cycles '%s'\n",
									configFile, lineNum, key);
							goto error;
						}
					}
					else continue;
				} else if (!strcmp(key, "hourly")) {
					newlog->criterium = ROT_HOURLY;
				} else if (!strcmp(key, "daily")) {
					newlog->criterium = ROT_DAYS;
					newlog->threshhold = 1;
				} else if (!strcmp(key, "monthly")) {
					newlog->criterium = ROT_MONTHLY;
				} else if (!strcmp(key, "weekly")) {
					unsigned weekday;
					char tmp;
					newlog->criterium = ROT_WEEKLY;
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
					newlog->criterium = ROT_YEARLY;
				} else if (!strcmp(key, "rotate")) {
					free(key);
					if ((key = isolateValue
						(configFile, lineNum, "rotate count", &start,
						&buf, length)) != NULL) {

						newlog->rotateCount = strtoul(key, &chptr, 0);
						if (*chptr || newlog->rotateCount < 0) {
							message(MESS_ERROR,
								"%s:%d bad rotation count '%s'\n",
								configFile, lineNum, key);
							RAISE_ERROR();
						}
					}
					else continue;
				} else if (!strcmp(key, "start")) {
					free(key);
					if ((key = isolateValue
						(configFile, lineNum, "start count", &start,
						&buf, length)) != NULL) {

						newlog->logStart = strtoul(key, &chptr, 0);
						if (*chptr || newlog->logStart < 0) {
							message(MESS_ERROR, "%s:%d bad start count '%s'\n",
								configFile, lineNum, key);
							RAISE_ERROR();
						}
					}
					else continue;
				} else if (!strcmp(key, "minage")) {
					free(key);
					if ((key = isolateValue
						(configFile, lineNum, "minage count", &start,
						&buf, length)) != NULL) {
						newlog->rotateMinAge = strtoul(key, &chptr, 0);
						if (*chptr || newlog->rotateMinAge < 0) {
							message(MESS_ERROR, "%s:%d bad minimum age '%s'\n",
								configFile, lineNum, start);
							RAISE_ERROR();
						}
					}
					else continue; 
				} else if (!strcmp(key, "maxage")) {
					free(key);
					if ((key = isolateValue
						(configFile, lineNum, "maxage count", &start,
						&buf, length)) != NULL) {
						newlog->rotateAge = strtoul(key, &chptr, 0);
						if (*chptr || newlog->rotateAge < 0) {
							message(MESS_ERROR, "%s:%d bad maximum age '%s'\n",
								configFile, lineNum, start);
							RAISE_ERROR();
						}
					}
					else continue;
				} else if (!strcmp(key, "errors")) {
					message(MESS_DEBUG,
						"%s: %d: the errors directive is deprecated and no longer used.\n",
						configFile, lineNum);
				} else if (!strcmp(key, "mail")) {
					freeLogItem(logAddress);
					if (!(newlog->logAddress = readAddress(configFile, lineNum,
										"mail", &start, &buf, length))) {
						RAISE_ERROR();
					}
					else continue;
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
					if (newlog != defConfig) {
						message(MESS_ERROR,
							"%s:%d tabooext may not appear inside "
							"of log file definition\n", configFile,
							lineNum);
						state = STATE_ERROR;
						continue;
					}
					free(key);
					if ((key = isolateValue(configFile, lineNum, "tabooext", &start,
							&buf, length)) != NULL) {
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
							int bytes;
							char *pattern = NULL;

							chptr = endtag;
							while (!isspace((unsigned char)*chptr) && *chptr != ',' && *chptr)
								chptr++;

							tabooPatterns = realloc(tabooPatterns, sizeof(*tabooPatterns) *
										(tabooCount + 1));
							bytes = asprintf(&pattern, "*%.*s", (int)(chptr - endtag), endtag);

							/* should test for malloc() failure */
							assert(bytes != -1);
							tabooPatterns[tabooCount] = pattern;
							tabooCount++;

							endtag = chptr;
							if (*endtag == ',')
								endtag++;
							while (*endtag && isspace((unsigned char)*endtag))
								endtag++;
						}
					}
					else continue;
				} else if (!strcmp(key, "taboopat")) {
					if (newlog != defConfig) {
						message(MESS_ERROR,
							"%s:%d taboopat may not appear inside "
							"of log file definition\n", configFile,
							lineNum);
						state = STATE_ERROR;
						continue;
					}
					free(key);
					if ((key = isolateValue(configFile, lineNum, "taboopat", &start,
							&buf, length)) != NULL) {
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
							int bytes;
							char *pattern = NULL;

							chptr = endtag;
							while (!isspace((unsigned char)*chptr) && *chptr != ',' && *chptr)
								chptr++;

							tabooPatterns = realloc(tabooPatterns, sizeof(*tabooPatterns) *
										(tabooCount + 1));
							bytes = asprintf(&pattern, "%.*s", (int)(chptr - endtag), endtag);

							/* should test for malloc() failure */
							assert(bytes != -1);
							tabooPatterns[tabooCount] = pattern;
							tabooCount++;

							endtag = chptr;
							if (*endtag == ',')
								endtag++;
							while (*endtag && isspace((unsigned char)*endtag))
								endtag++;
						}
					}
					else continue;
				} else if (!strcmp(key, "include")) {
					free(key);
					if ((key = isolateValue(configFile, lineNum, "include", &start,
							&buf, length)) != NULL) {

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
					}
					else continue;
				} else if (!strcmp(key, "olddir")) {
					freeLogItem (oldDir);

					if (!(newlog->oldDir = readPath(configFile, lineNum,
									"olddir", &start, &buf, length))) {
						RAISE_ERROR();
					}
					message(MESS_DEBUG, "olddir is now %s\n", newlog->oldDir);
				} else if (!strcmp(key, "extension")) {
					if ((key = isolateValue
						(configFile, lineNum, "extension name", &start,
							&buf, length)) != NULL) {
						freeLogItem (extension);
						newlog->extension = key;
						key = NULL;
					}
					else continue;

					message(MESS_DEBUG, "extension is now %s\n",
						newlog->extension);

				} else if (!strcmp(key, "addextension")) {
					if ((key = isolateValue
						(configFile, lineNum, "addextension name", &start,
							&buf, length)) != NULL) {
						freeLogItem (addextension);
						newlog->addextension = key;
						key = NULL;
					}
					else continue;

					message(MESS_DEBUG, "addextension is now %s\n",
						newlog->addextension);

				} else if (!strcmp(key, "compresscmd")) {
					char *compresscmd_base;
					freeLogItem (compress_prog);

					if (!
						(newlog->compress_prog =
							readPath(configFile, lineNum, "compress", &start, &buf, length))) {
						RAISE_ERROR();
					}

					if (access(newlog->compress_prog, X_OK)) {
						message(MESS_ERROR,
							"%s:%d compression program %s is not an executable file\n",
							configFile, lineNum, newlog->compress_prog);
						RAISE_ERROR();
					}

					message(MESS_DEBUG, "compress_prog is now %s\n",
						newlog->compress_prog);

					compresscmd_base = strdup(basename(newlog->compress_prog));
					/* we check whether we changed the compress_cmd. In case we use the apropriate extension
					   as listed in compress_cmd_list */
					for(i = 0; i < compress_cmd_list_size; i++) {
						if (!strcmp(compress_cmd_list[i].cmd, compresscmd_base)) {
							newlog->compress_ext = strdup((char *)compress_cmd_list[i].ext);
							message(MESS_DEBUG, "compress_ext was changed to %s\n", newlog->compress_ext);
							break;
						}
					}
					free(compresscmd_base);
				} else if (!strcmp(key, "uncompresscmd")) {
					freeLogItem (uncompress_prog);

					if (!
						(newlog->uncompress_prog =
							readPath(configFile, lineNum, "uncompress",
								&start, &buf, length))) {
						RAISE_ERROR();
					}

					if (access(newlog->uncompress_prog, X_OK)) {
						message(MESS_ERROR,
							"%s:%d uncompression program %s is not an executable file\n",
							configFile, lineNum, newlog->uncompress_prog);
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
				free(key);
				key = NULL;
			} else if (*start == '/' || *start == '"' || *start == '\''
#ifdef GLOB_TILDE
                                                                           || *start == '~'
#endif
                                                                           ) {
				char *local_key;
				size_t glob_count;
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

				/* Allocate a new logInfo structure and insert it into the logs
				queue, copying the actual values from defConfig */
				if ((newlog = newLogInfo(defConfig)) == NULL)
					goto error;

				endtag = start;
				while (endtag - buf < length && *endtag != '{' && *endtag != '}' && *endtag != '\0') {
					endtag++;}
				if (endtag - buf > length)
					continue;
				if (*endtag == '}') {
					message(MESS_ERROR, "%s:%d unexpected } (missing previous '{')\n", configFile,
						lineNum);
					goto error;
				}
				if (*endtag == '{') {
					in_config = 1;
				}
				else {
					message(MESS_ERROR, "%s:%d missing '{' after log files definition\n", configFile,
						lineNum);
					goto error;
				}
				local_key = strndup(start, endtag - start);
				start = endtag;

				if (poptParseArgvString(local_key, &argc, &argv)) {
				message(MESS_ERROR, "%s:%d error parsing filename\n",
					configFile, lineNum);
				free(local_key);
				goto error;
				} else if (argc < 1) {
				message(MESS_ERROR,
					"%s:%d { expected after log file name(s)\n",
					configFile, lineNum);
				free(local_key);
				goto error;
				}

				newlog->files = NULL;
				newlog->numFiles = 0;
				for (argNum = 0; argNum < argc; argNum++) {
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

				newlog->files =
					realloc(newlog->files,
						sizeof(*newlog->files) * (newlog->numFiles +
									globResult.
									gl_pathc));

				for (glob_count = 0; glob_count < globResult.gl_pathc; glob_count++) {
					/* if we glob directories we can get false matches */
					if (!lstat(globResult.gl_pathv[glob_count], &sb) &&
					S_ISDIR(sb.st_mode)) {
						continue;
					}

					for (log = logs.tqh_first; log != NULL;
						log = log->list.tqe_next) {
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
					newlog->numFiles++;
				}
		duperror:
				globfree(&globResult);
				}

				newlog->pattern = local_key;

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
				for (i = 0; i < newlog->numFiles; i++) {
					char *ld;
					char *dirpath;

					dirpath = strdup(newlog->files[i]);
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
					ld = alloca(strlen(dirName) + strlen(newlog->oldDir) + 2);
					sprintf(ld, "%s/%s", dirName, newlog->oldDir);
					free(dirpath);

					if (newlog->oldDir[0] != '/') {
						dirName = ld;
					}
					else {
						dirName = newlog->oldDir;
					}

					if (stat(dirName, &sb)) {
						if (errno == ENOENT && newlog->flags & LOG_FLAG_OLDDIRCREATE) {
							if (mkpath(dirName, newlog->olddirMode,
								newlog->olddirUid, newlog->olddirGid)) {
								goto error;
							}
						}
						else {
							message(MESS_ERROR, "%s:%d error verifying olddir "
								"path %s: %s\n", configFile, lineNum,
								dirName, strerror(errno));
							goto error;
						}
					}

					if (sb.st_dev != sb2.st_dev
						&& !(newlog->flags & (LOG_FLAG_COPYTRUNCATE | LOG_FLAG_COPY | LOG_FLAG_TMPFILENAME))) {
						message(MESS_ERROR,
							"%s:%d olddir %s and log file %s "
							"are on different devices\n", configFile,
							lineNum, newlog->oldDir, newlog->files[i]);
						goto error;
					}
				}
			}

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
				state = state & STATE_SKIP_CONFIG ? STATE_SKIP_CONFIG : STATE_DEFAULT;
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
				state = STATE_SKIP_LINE | (state & STATE_SKIP_CONFIG ? STATE_SKIP_CONFIG : 0);
			}
			else
				state = state & STATE_SKIP_CONFIG ? STATE_SKIP_CONFIG : STATE_DEFAULT;
			break;
		case STATE_ERROR:
			assert(newlog != defConfig);

			message(MESS_ERROR, "found error in %s, skipping\n",
				newlog->pattern ? newlog->pattern : "log config");

			state = STATE_SKIP_CONFIG;
			break;
		case STATE_LOAD_SCRIPT:
		case STATE_LOAD_SCRIPT | STATE_SKIP_CONFIG:
			if ((key = isolateWord(&start, &buf, length)) == NULL)
				continue;

			if (strcmp(key, "endscript") == 0) {
				if (state & STATE_SKIP_CONFIG) {
					state = STATE_SKIP_CONFIG;
				}
				else {
					endtag = start - 9;
					while (*endtag != '\n')
					endtag--;
					endtag++;
					*scriptDest = malloc(endtag - scriptStart + 1);
					strncpy(*scriptDest, scriptStart,
						endtag - scriptStart);
					(*scriptDest)[endtag - scriptStart] = '\0';

					scriptDest = NULL;
					scriptStart = NULL;
				}
				state = state & STATE_SKIP_CONFIG ? STATE_SKIP_CONFIG : STATE_DEFAULT;
			}
			else {
				state = (*start == '\n' ? 0 : STATE_SKIP_LINE) |
					STATE_LOAD_SCRIPT |
					(state & STATE_SKIP_CONFIG ? STATE_SKIP_CONFIG : 0);
			}
			break;
		case STATE_SKIP_CONFIG:
			if (*start == '}') {
				state = STATE_DEFAULT;
				freeTailLogs(1);
				newlog = defConfig;
			}
			else {
				if ((key = isolateWord(&start, &buf, length)) == NULL)
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
				free(key);
				key = NULL;
			}
			break;
		default:
			message(MESS_DEBUG,
				"%s: %d: readConfigFile() unknown state\n",
				configFile, lineNum);
	}
	if (key) {
		free(key);
		key = NULL;
	}
	if (*start == '\n') {
	    lineNum++;
	}

    }
 
    if (scriptStart) {
	message(MESS_ERROR,
		"%s:prerotate, postrotate or preremove without endscript\n",
		configFile);
	goto error;
    }

	munmap(buf, (size_t) length);
	close(fd);
    return logerror;
error:
	/* free is a NULL-safe operation */
	free(key);
	munmap(buf, (size_t) length);
	close(fd);
    return 1;
}
