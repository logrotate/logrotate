#include <sys/queue.h>
#include <alloca.h>
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

#include "basenames.h"
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

#if !defined(asprintf)
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

#if !defined(strndup)
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

enum {
	STATE_DEFAULT = 2,
	STATE_SKIP_LINE = 4,
	STATE_DEFINITION_END = 8,
	STATE_SKIP_CONFIG = 16,
	STATE_LOAD_SCRIPT = 32,
	STATE_ERROR = 64,
};

static char *defTabooExts[] = { ".rpmsave", ".rpmorig", "~", ",v",
    ".disabled", ".dpkg-old", ".dpkg-dist", ".dpkg-new", ".cfsaved",
    ".ucf-old", ".ucf-dist", ".ucf-new",
    ".rpmnew", ".swp", ".cfsaved", ".rhn-cfg-tmp-*"
};
static int defTabooCount = sizeof(defTabooExts) / sizeof(char *);

/* I shouldn't use globals here :-( */
static char **tabooExts = NULL;
int tabooCount = 0;
static int glob_errno = 0;

static int readConfigFile(const char *configFile, struct logInfo *defConfig);
static int globerr(const char *pathname, int theerr);

static char *isolateLine(char **strt, char **buf, size_t length) {
	char *endtag, *start, *tmp;
	start = *strt;
	endtag = start;
	while (endtag - *buf < length && *endtag != '\n') {
		endtag++;}
	if (endtag - *buf > length)
		return NULL;
	tmp = endtag - 1;
	while (isspace(*endtag))
		endtag--;
	char *key = strndup(start, endtag - start + 1);
	*strt = tmp;
	return key;
}

static char *isolateValue(const char *fileName, int lineNum, char *key,
			char **startPtr, char **buf, size_t length)
{
    char *chptr = *startPtr;

    while (chptr - *buf < length && isblank(*chptr))
	chptr++;
    if (chptr - *buf < length && *chptr == '=') {
	chptr++;
	while ( chptr - *buf < length && isblank(*chptr))
	    chptr++;
    }

    if (chptr - *buf < length && *chptr == '\n') {
		message(MESS_ERROR, "%s:%d argument expected after %s\n",
			fileName, lineNum, key);
		return NULL;
    }

	*startPtr = chptr;
	return isolateLine(startPtr, buf, length);
}

static char *isolateWord(char **strt, char **buf, size_t length) {
	char *endtag, *start;
	start = *strt;
	while (start - *buf < length && isblank(*start))
		start++;
	endtag = start;
	while (endtag - *buf < length && isalpha(*endtag)) {
		endtag++;}
	if (endtag - *buf > length)
		return NULL;
	char *key = strndup(start, endtag - start);
	*strt = endtag;
	return key;
}

static char *readPath(const char *configFile, int lineNum, char *key,
		      char **startPtr, char **buf, size_t length)
{
    char *chptr;
    char *start = *startPtr;
    char *path;

    wchar_t pwc;
    size_t len;

    if ((start = isolateValue(configFile, lineNum, key, startPtr, buf, length)) != NULL) {

	chptr = start;

	while( (len = mbrtowc(&pwc, chptr, strlen(chptr), NULL)) != 0 ) {
		if( len == (size_t)(-1) || len == (size_t)(-2) || !iswprint(pwc) || iswblank(pwc) ) {
		    message(MESS_ERROR, "%s:%d bad %s path %s\n",
			    configFile, lineNum, key, start);
		    return NULL;
		}
		chptr += len;
	}

/*
	while (*chptr && isprint(*chptr) && *chptr != ' ')
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

static char *readAddress(const char *configFile, int lineNum, char *key,
			 char **startPtr, char **buf, size_t length)
{
    char *endtag, *chptr;
    char *start = *startPtr;
    char *address;
	
    if ((endtag = isolateValue(configFile, lineNum, key, startPtr, buf, length)) != NULL) {

	chptr = endtag;
	while (*chptr && isprint(*chptr) && *chptr != ' ') {
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

static int checkFile(const char *fname)
{
	int i;
	char *pattern;

	/* Check if fname is '.' or '..'; if so, return false */
	if (fname[0] == '.' && (!fname[1] || (fname[1] == '.' && !fname[2])))
		return 0;

	/* Check if fname is ending in a taboo-extension; if so, return false */
	for (i = 0; i < tabooCount; i++) {
		asprintf(&pattern, "*%s", tabooExts[i]);
		if (!fnmatch(pattern, fname, 0))
		{
			free(pattern);
			message(MESS_DEBUG, "Ignoring %s, because of %s ending\n",
					fname, tabooExts[i]);
			return 0;
		}
	}
	free(pattern);
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
    to->threshhold = from->threshhold;
    to->minsize = from->minsize;
	to->maxsize = from->maxsize;
    to->rotateCount = from->rotateCount;
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
    to->createMode = from->createMode;
    to->createUid = from->createUid;
    to->createGid = from->createGid;
    to->suUid = from->suUid;
    to->suGid = from->suGid;
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
		removeLogInfo(*(logs.tqh_last));
}

static int readConfigPath(const char *path, struct logInfo *defConfig)
{
    struct stat sb;
    int here, oldnumlogs, result = 1;
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
	    oldnumlogs = numLogs;
	    copyLogInfo(&defConfigBackup, defConfig);
	    if (readConfigFile(namelist[i], defConfig)) {
		message(MESS_ERROR, "found error in file %s, skipping\n", namelist[i]);
		freeTailLogs(numLogs - oldnumlogs);
		freeLogInfo(defConfig);
		copyLogInfo(defConfig, &defConfigBackup);
		freeLogInfo(&defConfigBackup);
		continue;
	    } else {
		result = 0;
	    }
	    freeLogInfo(&defConfigBackup);
	}

	if (fchdir(here) < 0) {
		message(MESS_ERROR, "could not change directory to '.'");
	}
	close(here);
	free_2d_array(namelist, files_count);
    } else {
    	oldnumlogs = numLogs;
	copyLogInfo(&defConfigBackup, defConfig);
	if (readConfigFile(path, defConfig)) {
	    freeTailLogs(numLogs - oldnumlogs);
	    freeLogInfo(defConfig);
	    copyLogInfo(defConfig, &defConfigBackup);
	} else {
	    result = 0;
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
		.rotateAge = 0,
		.logStart = -1,
		.pre = NULL,
		.post = NULL,
		.first = NULL,
		.last = NULL,
		.logAddress = NULL,
		.extension = NULL,
		.compress_prog = NULL,
		.uncompress_prog = NULL,
		.compress_ext = NULL,
		.dateformat = NULL,
		.flags = LOG_FLAG_IFEMPTY,
		.shred_cycles = 0,
		.createMode = NO_MODE,
		.createUid = NO_UID,
		.createGid = NO_GID,
		.compress_options_list = NULL,
		.compress_options_count = 0
    };

    tabooExts = malloc(sizeof(*tabooExts) * defTabooCount);
    for (i = 0; i < defTabooCount; i++) {
	if ((tabooExts[i] = (char *) malloc(strlen(defTabooExts[i]) + 1))) {
	    strcpy(tabooExts[i], defTabooExts[i]);
	    tabooCount++;
	} else {
	    free_2d_array(tabooExts, tabooCount);
	    message(MESS_ERROR, "cannot malloc: %s\n", strerror(errno));
	    return 1;
	}
    }

    for (file = paths; *file; file++) {
	if (readConfigPath(*file, &defConfig)) {
	    result = 1;
	    break;
	}
    }
    free_2d_array(tabooExts, tabooCount);
    freeLogInfo(&defConfig);
    return result;
}

static int globerr(const char *pathname, int theerr)
{
    glob_errno = theerr;

    /* We want the glob operation to abort on error, so return 1 */
    return 1;
}

#define freeLogItem(what) \
	do { \
		free(newlog->what); \
		newlog->what = NULL; \
	} while (0);
#define MAX_NESTING 16U

static int readConfigFile(const char *configFile, struct logInfo *defConfig)
{
    int fd;
    char *buf, *endtag, *key = NULL;
    char foo;
    off_t length;
    int lineNum = 1;
    int multiplier;
    int i, k;
    char *scriptStart = NULL;
    char **scriptDest = NULL;
    struct logInfo *newlog = defConfig;
    char *start, *chptr;
    char *dirName;
    struct group *group;
    struct passwd *pw = NULL;
    int rc;
    char createOwner[200], createGroup[200];
    int createMode;
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
		return 1;
	}
	flags |= FD_CLOEXEC;
	if (fcntl(fd, F_SETFD, flags) == -1) {
		message(MESS_ERROR, "Could not set flags on file %s\n",
				configFile);
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
		return 1;
	}

	if (getuid() == ROOT_UID) {
		if ((sb.st_mode & 07533) != 0400) {
			message(MESS_DEBUG,
				"Ignoring %s because of bad file mode.\n",
				configFile);
			close(fd);
			return 0;
		}

		if ((pw = getpwnam("root")) == NULL) {
			message(MESS_DEBUG,
				"Ignoring %s because there's no password entry for the owner.\n",
				configFile);
			close(fd);
			return 0;
		}

		if (sb.st_uid != ROOT_UID && (pw == NULL ||
				sb.st_uid != pw->pw_uid ||
				strcmp("root", pw->pw_name) != 0)) {
			message(MESS_DEBUG,
				"Ignoring %s because the file owner is wrong (should be root).\n",
				configFile);
			close(fd);
			return 0;
		}
	}

	length = sb.st_size;

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

#ifdef MADV_DONTFORK
	madvise(buf, (size_t)(length + 2),
			MADV_SEQUENTIAL | MADV_WILLNEED | MADV_DONTFORK);
#else /* MADV_DONTFORK */
	madvise(buf, (size_t)(length + 2),
			MADV_SEQUENTIAL | MADV_WILLNEED);
#endif /* MADV_DONTFORK */

    message(MESS_DEBUG, "reading config file %s\n", configFile);

	start = buf;
    for (start = buf; start - buf < length; start++) {
	if (key) {
		free(key);
		key = NULL;
	}
	switch (state) {
		case STATE_DEFAULT:
			if (isblank(*start))
				continue;
			/* Skip comment */
			if (*start == '#') {
				state = STATE_SKIP_LINE;
				continue;
			}
			
			if (isalpha(*start)) {
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
					free(key);
					key = isolateLine(&start, &buf, length);
					if (key == NULL)
						continue;

					rc = sscanf(key, "%200s %200s%c", createOwner,
								createGroup, &foo);
					if (rc == 3) {
						message(MESS_ERROR, "%s:%d extra arguments for "
							"su\n", configFile, lineNum);
						if (newlog != defConfig) {
							state = STATE_ERROR;
							continue;
						} else {
							goto error;
						}
					}

					if (rc > 0) {
						pw = getpwnam(createOwner);
						if (!pw) {
							message(MESS_ERROR, "%s:%d unknown user '%s'\n",
								configFile, lineNum, createOwner);
							if (newlog != defConfig) {
								state = STATE_ERROR;
								continue;
							} else {
								goto error;
							}
						}
						newlog->suUid = pw->pw_uid;
						endpwent();
					}
					if (rc > 1) {
						group = getgrnam(createGroup);
						if (!group) {
							message(MESS_ERROR, "%s:%d unknown group '%s'\n",
								configFile, lineNum, createGroup);
							if (newlog != defConfig) {
								state = STATE_ERROR;
								continue;
							} else {
								goto error;
							}
						}
						newlog->suGid = group->gr_gid;
						endgrent();
					}

					newlog->flags |= LOG_FLAG_SU;
				} else if (!strcmp(key, "create")) {
					free(key);
					key = isolateLine(&start, &buf, length);
					if (key == NULL)
						continue;

					rc = sscanf(key, "%o %200s %200s%c", &createMode,
							createOwner, createGroup, &foo);
					if (rc == 4) {
						message(MESS_ERROR, "%s:%d extra arguments for "
							"create\n", configFile, lineNum);
						if (newlog != defConfig) {
							state = STATE_ERROR;
							continue;
						} else {
							goto error;
						}
					}

					if (rc > 0)
						newlog->createMode = createMode;

					if (rc > 1) {
						pw = getpwnam(createOwner);
						if (!pw) {
							message(MESS_ERROR, "%s:%d unknown user '%s'\n",
								configFile, lineNum, createOwner);
							if (newlog != defConfig) {
								state = STATE_ERROR;
								continue;
							} else {
								goto error;
							}
						}
						newlog->createUid = pw->pw_uid;
						endpwent();
					}
					if (rc > 2) {
						group = getgrnam(createGroup);
						if (!group) {
							message(MESS_ERROR, "%s:%d unknown group '%s'\n",
								configFile, lineNum, createGroup);
							if (newlog != defConfig) {
								state = STATE_ERROR;
								continue;
							} else {
								goto error;
							}
						}
						newlog->createGid = group->gr_gid;
						endgrent();
					}

					newlog->flags |= LOG_FLAG_CREATE;
				} else if (!strcmp(key, "nocreate")) {
					newlog->flags &= ~LOG_FLAG_CREATE;
				} else if (!strcmp(key, "size") || !strcmp(key, "minsize") ||
							!strcmp(key, "maxsize")) {
					unsigned long long size = 0;
					char *opt = key;
							
					if ((key = isolateValue(configFile, lineNum, opt, &start,
							&buf, length)) != NULL) {
						int l = strlen(key) - 1;
						if (key[l] == 'k') {
							key[l] = '\0';
							multiplier = 1024;
						} else if (key[l] == 'M') {
							key[l] = '\0';
							multiplier = 1024 * 1024;
						} else if (key[l] == 'G') {
							key[l] = '\0';
							multiplier = 1024 * 1024 * 1024;
						} else if (!isdigit(key[l])) {
							free(opt);
							message(MESS_ERROR, "%s:%d unknown unit '%c'\n",
								configFile, lineNum, key[l]);
							if (newlog != defConfig) {
								state = STATE_ERROR;
								continue;
							} else {
								goto error;
							}
						} else {
							multiplier = 1;
						}

						size = multiplier * strtoul(key, &chptr, 0);
						if (*chptr) {
							message(MESS_ERROR, "%s:%d bad size '%s'\n",
								configFile, lineNum, key);
							free(opt);
							if (newlog != defConfig) {
								state = STATE_ERROR;
								continue;
							} else {
								goto error;
							}
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
				} else if (!strcmp(key, "daily")) {
					newlog->criterium = ROT_DAYS;
					newlog->threshhold = 1;
				} else if (!strcmp(key, "monthly")) {
					newlog->criterium = ROT_MONTHLY;
				} else if (!strcmp(key, "weekly")) {
					newlog->criterium = ROT_WEEKLY;
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
							if (newlog != defConfig) {
								state = STATE_ERROR;
								continue;
							} else {
								goto error;
							}
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
							if (newlog != defConfig) {
								state = STATE_ERROR;
								continue;
							} else {
								goto error;
							}
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
							if (newlog != defConfig) {
								state = STATE_ERROR;
								continue;
							} else {
								goto error;
							}
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
						if (newlog != defConfig) {
						state = STATE_ERROR;
						continue;
						} else {
						goto error;
						}
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
							while (isspace(*endtag) && *endtag)
								endtag++;
						} else {
							free_2d_array(tabooExts, tabooCount);
							tabooCount = 0;
							tabooExts = malloc(1);
						}


						while (*endtag) {
							chptr = endtag;
							while (!isspace(*chptr) && *chptr != ',' && *chptr)
								chptr++;

							tabooExts = realloc(tabooExts, sizeof(*tabooExts) *
										(tabooCount + 1));
							tabooExts[tabooCount] = malloc(chptr - endtag + 1);
							strncpy(tabooExts[tabooCount], endtag,
								chptr - endtag);
							tabooExts[tabooCount][chptr - endtag] = '\0';
							tabooCount++;

							endtag = chptr;
							if (*endtag == ',')
								start++;
							while (isspace(*endtag) && *endtag)
								endtag++;
						}
					}
					else continue;
				} else if (!strcmp(key, "include")) {
					free(key);
					if ((key = isolateValue(configFile, lineNum, "include", &start,
							&buf, length)) != NULL) {

						message(MESS_DEBUG, "including %s\n", key);
						if (++recursion_depth > MAX_NESTING) {
							message(MESS_ERROR, "%s:%d include nesting too deep\n",
									configFile, lineNum);
							--recursion_depth;
							goto error;
						}
						if (readConfigPath(key, newlog)) {
							--recursion_depth;
							goto error;
						}
						--recursion_depth;
					}
					else continue;
				} else if (!strcmp(key, "olddir")) {
					freeLogItem (oldDir);

					if (!(newlog->oldDir = readPath(configFile, lineNum,
									"olddir", &start, &buf, length))) {
						if (newlog != defConfig) {
							state = STATE_ERROR;
							continue;
						} else {
							goto error;
						}
					}

#if 0
					if (stat(newlog->oldDir, &sb)) {
						message(MESS_ERROR, "%s:%d error verifying olddir "
							"path %s: %s\n", configFile, lineNum,
							newlog->oldDir, strerror(errno));
						free(newlog->oldDir);
						goto error;
					}

					if (!S_ISDIR(sb.st_mode)) {
						message(MESS_ERROR, "%s:%d olddir path %s is not a "
							"directory\n", configFile, lineNum,
							newlog->oldDir);
						free(newlog->oldDir);
						goto error;
					}
#endif
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

				} else if (!strcmp(key, "compresscmd")) {
					freeLogItem (compress_prog);

					if (!
						(newlog->compress_prog =
							readPath(configFile, lineNum, "compress", &start, &buf, length))) {
						if (newlog != defConfig) {
							state = STATE_ERROR;
							continue;
						} else {
							goto error;
						}
					}

					if (access(newlog->compress_prog, X_OK)) {
						message(MESS_ERROR,
							"%s:%d compression program %s is not an executable file\n",
							configFile, lineNum, newlog->compress_prog);
						if (newlog != defConfig) {
							state = STATE_ERROR;
							continue;
						} else {
							goto error;
						}
					}

					message(MESS_DEBUG, "compress_prog is now %s\n",
						newlog->compress_prog);

				} else if (!strcmp(key, "uncompresscmd")) {
					freeLogItem (uncompress_prog);

					if (!
						(newlog->uncompress_prog =
							readPath(configFile, lineNum, "uncompress",
								&start, &buf, length))) {
						if (newlog != defConfig) {
							state = STATE_ERROR;
							continue;
						} else {
							goto error;
						}
					}

					if (access(newlog->uncompress_prog, X_OK)) {
						message(MESS_ERROR,
							"%s:%d uncompression program %s is not an executable file\n",
							configFile, lineNum, newlog->uncompress_prog);
						if (newlog != defConfig) {
							state = STATE_ERROR;
							continue;
						} else {
							goto error;
						}
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

					if (!
						(options =
							readPath(configFile, lineNum, "compressoptions",
								&start, &buf, length))) {
						if (newlog != defConfig) {
							state = STATE_ERROR;
							continue;
						} else {
							goto error;
						}
					}

					if (poptParseArgvString(options,
								&newlog->compress_options_count,
								&newlog->compress_options_list)) {
						message(MESS_ERROR,
							"%s:%d invalid compression options\n",
							configFile, lineNum);
						free(options);
						if (newlog != defConfig) {
							state = STATE_ERROR;
							continue;
						} else {
							goto error;
						}
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
						if (newlog != defConfig) {
							state = STATE_ERROR;
							continue;
						} else {
							goto error;
						}
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
			} else if (*start == '/' || *start == '"' || *start == '\'') {
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
				while (endtag - buf < length && *endtag != '{' && *endtag != '\0') {
					endtag++;}
				if (endtag - buf > length)
					continue;
				char *key = strndup(start, endtag - start);
				start = endtag;

				if (poptParseArgvString(key, &argc, &argv)) {
				message(MESS_ERROR, "%s:%d error parsing filename\n",
					configFile, lineNum);
				free(key);
				goto error;
				} else if (argc < 1) {
				message(MESS_ERROR,
					"%s:%d { expected after log file name(s)\n",
					configFile, lineNum);
				free(key);
				goto error;
				}

				newlog->files = NULL;
				newlog->numFiles = 0;
				for (argNum = 0; argNum < argc && logerror != 1; argNum++) {
				if (globerr_msg) {
					free(globerr_msg);
					globerr_msg = NULL;
				}
					
				rc = glob(argv[argNum], GLOB_NOCHECK, globerr,
					&globResult);
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

				for (i = 0; i < globResult.gl_pathc; i++) {
					/* if we glob directories we can get false matches */
					if (!lstat(globResult.gl_pathv[i], &sb) &&
					S_ISDIR(sb.st_mode)) {
						continue;
					}

					for (log = logs.tqh_first; log != NULL;
						log = log->list.tqe_next) {
					for (k = 0; k < log->numFiles; k++) {
						if (!strcmp(log->files[k],
							globResult.gl_pathv[i])) {
						message(MESS_ERROR,
							"%s:%d duplicate log entry for %s\n",
							configFile, lineNum,
							globResult.gl_pathv[i]);
						logerror = 1;
						goto duperror;
						}
					}
					}

					newlog->files[newlog->numFiles] =
					strdup(globResult.gl_pathv[i]);
					newlog->numFiles++;
				}
		duperror:
				globfree(&globResult);
				}

				newlog->pattern = key;

// 				if (!logerror)
// 				message(MESS_DEBUG, "reading config info for %s\n", start);

				free(argv);

// 				start = endtag + 1;
			} else if (*start == '}') {
				if (newlog == defConfig) {
					message(MESS_ERROR, "%s:%d unexpected }\n", configFile,
						lineNum);
					goto error;
				}
			if (globerr_msg) {
				if (!(newlog->flags & LOG_FLAG_MISSINGOK))
					message(MESS_ERROR, "%s", globerr_msg);
				free(globerr_msg);
				globerr_msg = NULL;
				if (!(newlog->flags & LOG_FLAG_MISSINGOK))
					return 1;
				}

				if (newlog->oldDir) {
				for (i = 0; i < newlog->numFiles; i++) {
					char *ld;
					dirName = ourDirName(newlog->files[i]);
					if (stat(dirName, &sb2)) {
					message(MESS_ERROR,
						"%s:%d error verifying log file "
						"path %s: %s\n", configFile, lineNum,
						dirName, strerror(errno));
					free(dirName);
					goto error;
					}
					ld = alloca(strlen(dirName) + strlen(newlog->oldDir) +
						2);
					sprintf(ld, "%s/%s", dirName, newlog->oldDir);
					free(dirName);

					if (newlog->oldDir[0] != '/')
					dirName = ld;
					else
					dirName = newlog->oldDir;
					if (stat(dirName, &sb)) {
					message(MESS_ERROR, "%s:%d error verifying olddir "
						"path %s: %s\n", configFile, lineNum,
						dirName, strerror(errno));
					goto error;
					}

					if (sb.st_dev != sb2.st_dev) {
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
			if (isblank(*start))
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
					(strcmp(key, "firstrotate") == 0) ||
					(strcmp(key, "lastrotate") == 0)
					) {
					state = STATE_LOAD_SCRIPT | STATE_SKIP_CONFIG;
				}
				else {
					state = STATE_SKIP_LINE | STATE_SKIP_CONFIG;
				}
				free(key);
				key = NULL;
			}
			break;
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
		"%s:prerotate or postrotate without endscript\n",
		configFile);
	goto error;
    }

	munmap(buf, (size_t) length);
	close(fd);
    return 0;
error:
	if (key)
		free(key);
	munmap(buf, (size_t) length);
	close(fd);
    return 1;
}
