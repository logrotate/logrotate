#include "queue.h"
/* alloca() is defined in stdlib.h in NetBSD */
#ifndef __NetBSD__
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
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <glob.h>
#include <locale.h>
#include <sys/types.h>
#include <utime.h>
#include <stdint.h>

#if defined(SunOS)
#include <limits.h>
#endif

#include "basenames.h"
#include "log.h"
#include "logrotate.h"

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
static security_context_t prev_context = NULL;
int selinux_enabled = 0;
int selinux_enforce = 0;
#endif

#ifdef WITH_ACL
#include "sys/acl.h"
#define acl_type acl_t
#define ACL_NOT_WELL_SUPPORTED(Err) \
     ((Err) == ENOTSUP || (Err) == ENOSYS || (Err) == EINVAL || (Err) == EBUSY)
#else
#define acl_type void *
#endif

static acl_type prev_acl = NULL;

#if !defined(GLOB_ABORTED) && defined(GLOB_ABEND)
#define GLOB_ABORTED GLOB_ABEND
#endif

#ifdef PATH_MAX
#define STATEFILE_BUFFER_SIZE 2 * PATH_MAX + 16
#else
#define STATEFILE_BUFFER_SIZE 4096
#endif

#ifdef __hpux
extern int asprintf(char **str, const char *fmt, ...);
#endif

#if defined(HAVE_FORK)
#define FORK_OR_VFORK fork
#define DOEXIT exit
#elif defined(HAVE_VFORK)
#define FORK_OR_VFORK vfork
#define DOEXIT _exit
#else
#define FORK_OR_VFORK fork
#define DOEXIT exit
#endif

// Number of seconds in a day
#define DAY_SECONDS 86400

struct logState {
	char *fn;
	struct tm lastRotated;	/* only tm_hour, tm_mday, tm_mon, tm_year are good! */
	struct stat sb;
	int doRotate;
	int isUsed;	/* True if there is real log file in system for this state. */
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
	int prefix_len;
	const char *dformat;
};

struct logStates {
	LIST_HEAD(stateSet, logState) head;
} **states;

unsigned int hashSize;
int numLogs = 0;
int debug = 0;
char *mailCommand = DEFAULT_MAIL_COMMAND;
time_t nowSecs = 0;
static uid_t save_euid;
static gid_t save_egid;

static int shred_file(int fd, char *filename, struct logInfo *log);

static int globerr(const char *pathname, int theerr)
{
    message(MESS_ERROR, "error accessing %s: %s\n", pathname,
	    strerror(theerr));

    /* We want the glob operation to continue, so return 0 */
    return 1;
}

#if defined(HAVE_STRPTIME) && defined(HAVE_QSORT)

/* We could switch to qsort_r to get rid of this global variable,
 * but qsort_r is not portable enough (Linux vs. *BSD vs ...)... */
static struct compData _compData;

static int compGlobResult(const void *result1, const void *result2)  {
	struct tm time;
	time_t t1, t2;
	const char *r1 = *(const char **)(result1);
	const char *r2 = *(const char **)(result2);

	memset(&time, 0, sizeof(struct tm));
	strptime(r1 + _compData.prefix_len, _compData.dformat, &time);
	t1 = mktime(&time);

	memset(&time, 0, sizeof(struct tm));
	strptime(r2 + _compData.prefix_len, _compData.dformat, &time);
	t2 = mktime(&time);

	if (t1 < t2) return -1;
	if (t1 > t2) return  1;
	return 0;
}

static void sortGlobResult(glob_t *result, int prefix_len, const char *dformat) {
	if (!dformat || *dformat == '\0') {
		return;
	}

	_compData.prefix_len = prefix_len;
	_compData.dformat = dformat;
	qsort(result->gl_pathv, result->gl_pathc, sizeof(char *), compGlobResult);
}
#else
static void sortGlobResult(glob_t *result, int prefix_len, const char *dformat) {
	/* TODO */
}
#endif

int switch_user(uid_t user, gid_t group) {
	save_egid = getegid();
	save_euid = geteuid();
	if (save_euid == user && save_egid == group)
		return 0;
	message(MESS_DEBUG, "switching euid to %d and egid to %d\n",
		user, group);
	if (setegid(group) || seteuid(user)) {
		message(MESS_ERROR, "error switching euid to %d and egid to %d: %s\n",
			user, group, strerror(errno));
		return 1;
	}
	return 0;
}

int switch_user_permanently(const struct logInfo *log) {
	gid_t group = getegid();
	uid_t user = geteuid();
	if (!(log->flags & LOG_FLAG_SU)) {
		return 0;
	}
	if (getuid() == user && getgid() == group)
		return 0;
	// switch to full root first
	if (setgid(getgid()) || setuid(getuid())) {
		message(MESS_ERROR, "error getting rid of euid != uid\n");
		return 1;
	}
	message(MESS_DEBUG, "switching uid to %d and gid to %d\n",
		user, group);
	if (setgid(group) || setuid(user)) {
		message(MESS_ERROR, "error switching euid to %d and egid to %d: %s\n",
			user, group, strerror(errno));
		return 1;
	}
	return 0;
}

int switch_user_back() {
	return switch_user(save_euid, save_egid);
}

int switch_user_back_permanently() {
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
static int allocateHash(unsigned int hs)
{
	int i;

	/* Enforce some reasonable minimum hash size */
	if (hs < HASH_SIZE_MIN)
		hs = HASH_SIZE_MIN;

	message(MESS_DEBUG, "Allocating hash table for state file, size %d entries\n",
			hs);

	states = calloc(hs, sizeof(struct logStates *));
	if (states == NULL) {
		message(MESS_ERROR, "could not allocate memory for "
				"hash table\n");
		return 1;
	}

	for (i = 0; i < hs; i++) {
		states[i] = malloc(sizeof(struct logState));
		if (states[i] == NULL) {
			message(MESS_ERROR, "could not allocate memory for "
				"hash element\n");
			return 1;
		}
		LIST_INIT(&(states[i]->head));
	}

	hashSize = hs;

	return 0;
}

#define HASH_CONST 13
static unsigned hashIndex(const char *fn)
{
	unsigned hash = 0;

	while (*fn) {
		hash *= HASH_CONST;
		hash += *fn++;
	}

	return hash % hashSize;
}

static struct logState *newState(const char *fn)
{
	struct tm now = *localtime(&nowSecs);
	struct logState *new;
	time_t lr_time;

	message(MESS_DEBUG, "Creating new state\n");

	if ((new = malloc(sizeof(*new))) == NULL)
		return NULL;

	if ((new->fn = strdup(fn)) == NULL) {
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
	new->lastRotated = *localtime(&lr_time);

	return new;
}

static struct logState *findState(const char *fn)
{
	unsigned int i = hashIndex(fn);
	struct logState *p;

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

static int runScript(struct logInfo *log, char *logfn, char *script)
{
	int rc;

	if (debug) {
		message(MESS_DEBUG, "running script with arg %s: \"%s\"\n",
			logfn, script);
		return 0;
	}

	if (!FORK_OR_VFORK()) {
		if (log->flags & LOG_FLAG_SU) {
			if (switch_user_back_permanently() != 0) {
				DOEXIT(1);
			}
		}
		execl("/bin/sh", "sh", "-c", script, "logrotate_script", logfn, NULL);
		DOEXIT(1);
	}

	wait(&rc);
	return rc;
}

int createOutputFile(char *fileName, int flags, struct stat *sb, acl_type acl, int force_mode)
{
    int fd = -1;
    struct stat sb_create;
    int acl_set = 0;
    int i;

    for (i = 0; i < 2; ++i) {
	fd = open(fileName, (flags | O_EXCL | O_NOFOLLOW),
		(S_IRUSR | S_IWUSR) & sb->st_mode);

	if ((fd >= 0) || (errno != EEXIST))
	    break;

	/* the destination file already exists, while it should not */
	struct tm now = *localtime(&nowSecs);
	size_t fileName_size = strlen(fileName);
	size_t buf_size = fileName_size + sizeof("-YYYYMMDDHH.backup");
	char *backupName = alloca(buf_size);
	char *ptr = backupName;

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
 
    if ((sb_create.st_uid != sb->st_uid || sb_create.st_gid != sb->st_gid) && 
		fchown(fd, sb->st_uid, sb->st_gid)) {
	message(MESS_ERROR, "error setting owner of %s to uid %d and gid %d: %s\n",
		fileName, sb->st_uid, sb->st_gid, strerror(errno));
	close(fd);
	return -1;
    }

#ifdef WITH_ACL
	if (!force_mode && acl) {
		if (acl_set_fd(fd, acl) == -1) {
			if (!ACL_NOT_WELL_SUPPORTED(errno)) {
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

#define DIGITS 10

/* unlink, but try to call shred from GNU fileutils */
static int shred_file(int fd, char *filename, struct logInfo *log)
{
	char count[DIGITS];    /*  that's a lot of shredding :)  */
	const char **fullCommand;
	int id = 0;
	int status;

	if (log->preremove) {
	    message(MESS_DEBUG, "running preremove script\n");
	    if (runScript(log, filename, log->preremove)) {
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
		return unlink(filename);
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

	if (!FORK_OR_VFORK()) {
		dup2(fd, 1);
		close(fd);

		if (switch_user_permanently(log) != 0) {
			DOEXIT(1);
		}

		execvp(fullCommand[0], (void *) fullCommand);
		DOEXIT(1);
	}
	
	wait(&status);

	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
		message(MESS_ERROR, "Failed to shred %s\n, trying unlink", filename);
		return unlink(filename);
	}

	/* We have to unlink it after shred anyway,
	 * because it doesn't remove the file itself */
	return unlink(filename);
}

static int removeLogFile(char *name, struct logInfo *log)
{
	int fd;
	message(MESS_DEBUG, "removing old log %s\n", name);

	if ((fd = open(name, O_RDWR | O_NOFOLLOW)) < 0) {
		message(MESS_ERROR, "error opening %s: %s\n",
			name, strerror(errno));
		return 1;
	}

	if (!debug && shred_file(fd, name, log)) {
		message(MESS_ERROR, "Failed to remove old log %s: %s\n",
			name, strerror(errno));
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}

static int compressLogFile(char *name, struct logInfo *log, struct stat *sb)
{
    char *compressedName;
    char *envInFilename;
    const char **fullCommand;
    struct utimbuf utim;
    int inFile;
    int outFile;
    int i;
    int status;
    int compressPipe[2];
    char buff[4092];
    int error_printed = 0;

    message(MESS_DEBUG, "compressing log with: %s\n", log->compress_prog);
    if (debug)
	return 0;

    fullCommand = alloca(sizeof(*fullCommand) *
			 (log->compress_options_count + 2));
    fullCommand[0] = log->compress_prog;
    for (i = 0; i < log->compress_options_count; i++)
	fullCommand[i + 1] = log->compress_options_list[i];
    fullCommand[log->compress_options_count + 1] = NULL;

    compressedName = alloca(strlen(name) + strlen(log->compress_ext) + 2);
    sprintf(compressedName, "%s%s", name, log->compress_ext);

    if ((inFile = open(name, O_RDWR | O_NOFOLLOW)) < 0) {
	message(MESS_ERROR, "unable to open %s for compression\n", name);
	return 1;
    }

#ifdef WITH_ACL
	if ((prev_acl = acl_get_fd(inFile)) == NULL) {
		if (!ACL_NOT_WELL_SUPPORTED(errno)) {
			message(MESS_ERROR, "getting file ACL %s: %s\n",
				name, strerror(errno));
			close(inFile);
			return 1;
		}
	}
#endif

    outFile =
	createOutputFile(compressedName, O_RDWR | O_CREAT, sb, prev_acl, 0);
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

	if (pipe(compressPipe) < 0) {
		message(MESS_ERROR, "error opening pipe for compress: %s",
				strerror(errno));
		close(inFile);
		close(outFile);
		return 1;
	}

    if (!FORK_OR_VFORK()) {
	dup2(inFile, 0);
	close(inFile);
	dup2(outFile, 1);
	close(outFile);
	dup2(compressPipe[1], 2);
	close(compressPipe[0]);
	close(compressPipe[1]);

	if (switch_user_permanently(log) != 0) {
		DOEXIT(1);
	}

	envInFilename = alloca(strlen("LOGROTATE_COMPRESSED_FILENAME=") + strlen(name) + 2);
	sprintf(envInFilename, "LOGROTATE_COMPRESSED_FILENAME=%s", name);
	putenv(envInFilename);
	execvp(fullCommand[0], (void *) fullCommand);
	DOEXIT(1);
    }

    close(compressPipe[1]);
    while ((i = read(compressPipe[0], buff, sizeof(buff) - 1)) > 0) {
	if (!error_printed) {
	    error_printed = 1;
	    message(MESS_ERROR, "Compressing program wrote following message "
		    "to stderr when compressing log %s:\n", name);
	}
	buff[i] = '\0';
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

    utim.actime = sb->st_atime;
    utim.modtime = sb->st_mtime;
    utime(compressedName,&utim);
    /* If we can't change atime/mtime, it's not a disaster.
       It might possibly fail under SELinux. */

    shred_file(inFile, name, log);
    close(inFile);

    return 0;
}

static int mailLog(struct logInfo *log, char *logFile, char *mailCommand,
		   char *uncompressCommand, char *address, char *subject)
{
    int mailInput;
    pid_t mailChild, uncompressChild = 0;
    int mailStatus, uncompressStatus;
    int uncompressPipe[2];
    char *mailArgv[] = { mailCommand, "-s", subject, address, NULL };
    int rc = 0;

    if ((mailInput = open(logFile, O_RDONLY | O_NOFOLLOW)) < 0) {
	message(MESS_ERROR, "failed to open %s for mailing: %s\n", logFile,
		strerror(errno));
	return 1;
    }

    if (uncompressCommand) {
		if (pipe(uncompressPipe) < 0) {
			message(MESS_ERROR, "error opening pipe for uncompress: %s",
					strerror(errno));
			close(mailInput);
			return 1;
		}
		if (!(uncompressChild = FORK_OR_VFORK())) {
			/* uncompress child */
			dup2(mailInput, 0);
			close(mailInput);
			dup2(uncompressPipe[1], 1);
			close(uncompressPipe[0]);
			close(uncompressPipe[1]);

			if (switch_user_permanently(log) != 0) {
				DOEXIT(1);
			}

			execlp(uncompressCommand, uncompressCommand, NULL);
			DOEXIT(1);
		}

		close(mailInput);
		mailInput = uncompressPipe[0];
		close(uncompressPipe[1]);
    }

    if (!(mailChild = FORK_OR_VFORK())) {
	dup2(mailInput, 0);
	close(mailInput);
	close(1);

	// mail command runs as root
	if (log->flags & LOG_FLAG_SU) {
		if (switch_user_back_permanently() != 0) {
			DOEXIT(1);
		}
	}

	execvp(mailArgv[0], mailArgv);
	DOEXIT(1);
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

static int mailLogWrapper(char *mailFilename, char *mailCommand,
			  int logNum, struct logInfo *log)
{
	/* if the log is compressed (and we're not mailing a
	* file whose compression has been delayed), we need
	* to uncompress it */
	if ((log->flags & LOG_FLAG_COMPRESS) && !(log->flags & LOG_FLAG_DELAYCOMPRESS)) {
		if (mailLog(log, mailFilename, mailCommand,
			log->uncompress_prog, log->logAddress,
			(log->flags & LOG_FLAG_MAILFIRST) ? log->files[logNum] : mailFilename))
			return 1;
	} else {
		if (mailLog(log, mailFilename, mailCommand, NULL,
			log->logAddress, mailFilename))
			return 1;
	}
	return 0;
}

/* Use a heuristic to determine whether stat buffer SB comes from a file
   with sparse blocks.  If the file has fewer blocks than would normally
   be needed for a file of its size, then at least one of the blocks in
   the file is a hole.  In that case, return true.  */
static int is_probably_sparse(struct stat const *sb)
{
#if defined(HAVE_STRUCT_STAT_ST_BLOCKS) && defined(HAVE_STRUCT_STAT_ST_BLKSIZE)
	return (S_ISREG (sb->st_mode)
          && sb->st_blocks < sb->st_size / sb->st_blksize);
#else
	return 0;
#endif
}

#define MIN(a,b) ((a) < (b) ? (a) : (b))

/* Return whether the buffer consists entirely of NULs.
   Note the word after the buffer must be non NUL. */

static inline int is_nul (void const *buf, size_t bufsize)
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
		n_rw = write (fd, buf, count);
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

static int sparse_copy(int src_fd, int dest_fd, struct stat *sb,
		       const char *saveLog, const char *currLog)
{
	int make_holes = is_probably_sparse(sb);
	size_t max_n_read = SIZE_MAX;
	int last_write_made_hole = 0;
	off_t total_n_read = 0;
	char buf[BUFSIZ + 1];

	while (max_n_read) {
		int make_hole = 0;

		ssize_t n_read = read (src_fd, buf, MIN (max_n_read, BUFSIZ));
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

		max_n_read -= n_read;
		total_n_read += n_read;

		if (make_holes) {
			/* Sentinel required by is_nul().  */
			buf[n_read] = '\1';

			if ((make_hole = is_nul(buf, n_read))) {
				if (lseek (dest_fd, n_read, SEEK_CUR) < 0) {
					message(MESS_ERROR, "error seeking %s: %s\n",
						saveLog, strerror(errno));
					return 0;
				}
			}
		}

		if (!make_hole) {
			size_t n = n_read;
			if (full_write (dest_fd, buf, n) != n) {
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

static int copyTruncate(char *currLog, char *saveLog, struct stat *sb,
			int flags)
{
    int fdcurr = -1, fdsave = -1;

    message(MESS_DEBUG, "copying %s to %s\n", currLog, saveLog);

    if (!debug) {
	if ((fdcurr = open(currLog, ((flags & LOG_FLAG_COPY) ? O_RDONLY : O_RDWR) | O_NOFOLLOW)) < 0) {
	    message(MESS_ERROR, "error opening %s: %s\n", currLog,
		    strerror(errno));
	    return 1;
	}
#ifdef WITH_SELINUX
	if (selinux_enabled) {
	    security_context_t oldContext;
	    if (fgetfilecon_raw(fdcurr, &oldContext) >= 0) {
		if (getfscreatecon_raw(&prev_context) < 0) {
		    message(MESS_ERROR,
			    "getting default context: %s\n",
			    strerror(errno));
		    if (selinux_enforce) {
				freecon(oldContext);
				close(fdcurr);
				return 1;
		    }
		}
		if (setfscreatecon_raw(oldContext) < 0) {
		    message(MESS_ERROR,
			    "setting file context %s to %s: %s\n",
			    saveLog, oldContext, strerror(errno));
			if (selinux_enforce) {
				freecon(oldContext);
				close(fdcurr);
				return 1;
		    }
		}
		message(MESS_DEBUG, "set default create context\n");
		freecon(oldContext);
	    } else {
		    if (errno != ENOTSUP) {
			    message(MESS_ERROR, "getting file context %s: %s\n",
				    currLog, strerror(errno));
			    if (selinux_enforce) {
				    return 1;
			    }
		    }
	    }
	}
#endif
#ifdef WITH_ACL
	if ((prev_acl = acl_get_fd(fdcurr)) == NULL) {
		if (!ACL_NOT_WELL_SUPPORTED(errno)) {
			message(MESS_ERROR, "getting file ACL %s: %s\n",
				currLog, strerror(errno));
			close(fdcurr);
			return 1;
		}
	}
#endif /* WITH_ACL */
	fdsave =
	    createOutputFile(saveLog, O_WRONLY | O_CREAT, sb, prev_acl, 0);
#ifdef WITH_SELINUX
	if (selinux_enabled) {
	    setfscreatecon_raw(prev_context);
		freecon(prev_context);
		prev_context = NULL;
	}
#endif
#ifdef WITH_ACL
	if (prev_acl) {
		acl_free(prev_acl);
		prev_acl = NULL;
	}
#endif
	if (fdsave < 0) {
	    close(fdcurr);
	    return 1;
	}

	if (sparse_copy(fdcurr, fdsave, sb, saveLog, currLog) != 1) {
		close(fdcurr);
		close(fdsave);
		message(MESS_ERROR, "error copying %s to %s: %s\n", currLog,
				saveLog, strerror(errno));
		unlink(saveLog);
		return 1;
	}
    }

    if (flags & LOG_FLAG_COPYTRUNCATE) {
	message(MESS_DEBUG, "truncating %s\n", currLog);

	if (!debug) {
	    fsync(fdsave);
	    if (ftruncate(fdcurr, 0)) {
		message(MESS_ERROR, "error truncating %s: %s\n", currLog,
			strerror(errno));
		close(fdcurr);
		close(fdsave);
		return 1;
	    }
	}
    } else
	message(MESS_DEBUG, "Not truncating %s\n", currLog);

    if (fdcurr >= 0) {
	close(fdcurr);
    }
    if (fdsave >= 0) {
	close(fdsave);
    }
    return 0;
}

int findNeedRotating(struct logInfo *log, int logNum, int force)
{
    struct stat sb;
    struct logState *state = NULL;
    struct tm now = *localtime(&nowSecs);

    message(MESS_DEBUG, "considering log %s\n", log->files[logNum]);

	/* Check if parent directory of this log has safe permissions */
	if ((log->flags & LOG_FLAG_SU) == 0 && getuid() == 0) {
		char *ld = ourDirName(log->files[logNum]);
		if (stat(ld, &sb)) {
			/* If parent directory doesn't exist, it's not real error
			  (unless nomissingok is specified)
			  and rotation is not needed */
			if (errno != ENOENT || (errno == ENOENT && (log->flags & LOG_FLAG_MISSINGOK) == 0)) {
				message(MESS_ERROR, "stat of %s failed: %s\n", ld,
					strerror(errno));
				free(ld);
				return 1;
			}
			free(ld);
			return 0;
		}
		/* Don't rotate in directories writable by others or group which is not "root"  */
		if ((sb.st_gid != 0 && sb.st_mode & S_IWGRP) || sb.st_mode & S_IWOTH) {
			message(MESS_ERROR, "skipping \"%s\" because parent directory has insecure permissions"
								" (It's world writable or writable by group which is not \"root\")"
								" Set \"su\" directive in config file to tell logrotate which user/group"
								" should be used for rotation.\n"
								,log->files[logNum]);
			free(ld);
			return 1;
		}
		free(ld);
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
	state->doRotate = (sb.st_size >= log->threshhold);
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
	switch (log->criterium) {
	case ROT_WEEKLY:
	    /* rotate if:
	       1) the current weekday is before the weekday of the
	       last rotation
	       2) more then a week has passed since the last
	       rotation */
	    state->doRotate = ((now.tm_wday < state->lastRotated.tm_wday)
			       ||
			       ((mktime(&now) -
				 mktime(&state->lastRotated)) >
				(7 * 24 * 3600)));
	    if (!state->doRotate) {
	    message(MESS_DEBUG, "  log does not need rotating "
		    "(log has been rotated at %d-%d-%d %d:%d, "
		    "that is not week ago yet)\n", 1900 + state->lastRotated.tm_year,
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
		    "(log has been rotated at %d-%d-%d %d:%d, "
		    "that is not hour ago yet)\n", 1900 + state->lastRotated.tm_year,
		    1 + state->lastRotated.tm_mon, state->lastRotated.tm_mday,
		    state->lastRotated.tm_hour, state->lastRotated.tm_min);
	    }
	    break;
	case ROT_DAYS:
	    /* FIXME: only days=1 is implemented!! */
	    state->doRotate = ((now.tm_mday != state->lastRotated.tm_mday) ||
			    (now.tm_mon != state->lastRotated.tm_mon) ||
			    (now.tm_year != state->lastRotated.tm_year));
	    if (!state->doRotate) {
	    message(MESS_DEBUG, "  log does not need rotating "
		    "(log has been rotated at %d-%d-%d %d:%d, "
		    "that is not day ago yet)\n", 1900 + state->lastRotated.tm_year,
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
		    "(log has been rotated at %d-%d-%d %d:%d, "
		    "that is not month ago yet)\n", 1900 + state->lastRotated.tm_year,
		    1 + state->lastRotated.tm_mon, state->lastRotated.tm_mday,
		    state->lastRotated.tm_hour, state->lastRotated.tm_min);
	    }
	    break;
	case ROT_YEARLY:
	    /* rotate if the logs haven't been rotated this year */
	    state->doRotate = (now.tm_year != state->lastRotated.tm_year);
	    if (!state->doRotate) {
	    message(MESS_DEBUG, "  log does not need rotating "
		    "(log has been rotated at %d-%d-%d %d:%d, "
		    "that is not year ago yet)\n", 1900 + state->lastRotated.tm_year,
		    1 + state->lastRotated.tm_mon, state->lastRotated.tm_mday,
		    state->lastRotated.tm_hour, state->lastRotated.tm_min);
	    }
	    break;
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
	if (log->rotateMinAge && log->rotateMinAge >= (nowSecs - sb.st_mtime) / DAY_SECONDS) {
	    state->doRotate = 0;
	    message(MESS_DEBUG, "  log does not need rotating "
		    "('minage' directive is used and the log "
		    "age is smaller than the minage days)\n");
	}
    }
    else if (!state->doRotate) {
	message(MESS_DEBUG, "  log does not need rotating "
		"(log has been already rotated)\n");
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

int prerotateSingleLog(struct logInfo *log, int logNum, struct logState *state,
		       struct logNames *rotNames)
{
    struct tm now = *localtime(&nowSecs);
    char *oldName, *newName = NULL;
    char *compext = "";
    char *fileext = "";
    int hasErrors = 0;
    int i, j;
    char *glob_pattern;
    glob_t globResult;
    int rc;
    int rotateCount = log->rotateCount ? log->rotateCount : 1;
    int logStart = (log->logStart == -1) ? 1 : log->logStart;
#define DATEEXT_LEN 64
#define PATTERN_LEN (DATEEXT_LEN * 2)
    char dext_str[DATEEXT_LEN];
    char dformat[DATEEXT_LEN] = "";
    char dext_pattern[PATTERN_LEN];
    char *dext;

    if (!state->doRotate)
	return 0;

    /* Logs with rotateCounts of 0 are rotated once, then removed. This
       lets scripts run properly, and everything gets mailed properly. */

    message(MESS_DEBUG, "rotating log %s, log->rotateCount is %d\n",
	    log->files[logNum], log->rotateCount);

    if (log->flags & LOG_FLAG_COMPRESS)
	compext = log->compress_ext;

    state->lastRotated = now;

    if (log->oldDir) {
	if (log->oldDir[0] != '/') {
	    char *ld = ourDirName(log->files[logNum]);
	    rotNames->dirName =
		malloc(strlen(ld) + strlen(log->oldDir) + 2);
	    sprintf(rotNames->dirName, "%s/%s", ld, log->oldDir);
	    free(ld);
	} else
	    rotNames->dirName = strdup(log->oldDir);
    } else
	rotNames->dirName = ourDirName(log->files[logNum]);

    rotNames->baseName = strdup(ourBaseName(log->files[logNum]));

    if (log->addextension) {
	size_t baseLen = strlen(rotNames->baseName);
	size_t extLen = strlen(log->addextension);
	if (baseLen >= extLen &&
		strncmp(&(rotNames->baseName[baseLen - extLen]),
		    log->addextension, extLen) == 0) {
	    char *tempstr;

	    tempstr = calloc(baseLen - extLen + 1, sizeof(char));
	    strncat(tempstr, rotNames->baseName, baseLen - extLen);
	    free(rotNames->baseName);
	    rotNames->baseName = tempstr;
	}
	fileext = log->addextension;
    }

    if (log->extension &&
	strncmp(&
		(rotNames->
		 baseName[strlen(rotNames->baseName) -
			  strlen(log->extension)]), log->extension,
		strlen(log->extension)) == 0) {
	char *tempstr;

	fileext = log->extension;
	tempstr =
	    calloc(strlen(rotNames->baseName) - strlen(log->extension) + 1,
		   sizeof(char));
	strncat(tempstr, rotNames->baseName,
		strlen(rotNames->baseName) - strlen(log->extension));
	free(rotNames->baseName);
	rotNames->baseName = tempstr;
    }
	
    /* Adjust "now" if we want yesterday's date */
    if (log->flags & LOG_FLAG_DATEYESTERDAY) {
        now.tm_hour = 12; /* set hour to noon to work around DST issues */
        now.tm_mday = now.tm_mday - 1;
        mktime(&now);
    }

	/* Allow only %Y %d %m and create valid strftime format string
	 * Construct the glob pattern corresponding to the date format */
	dext_str[0] = '\0';
	if (log->dateformat) {
		i = j = 0;
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

#ifdef WITH_SELINUX
	if (selinux_enabled) {
	    security_context_t oldContext = NULL;
	    if (getfilecon_raw(log->files[logNum], &oldContext) > 0) {
			if (getfscreatecon_raw(&prev_context) < 0) {
				message(MESS_ERROR,
					"getting default context: %s\n",
					strerror(errno));
				if (selinux_enforce) {
					freecon(oldContext);
					return 1;
				}
			}
			if (setfscreatecon_raw(oldContext) < 0) {
				message(MESS_ERROR,
					"setting file context %s to %s: %s\n",
					log->files[logNum], oldContext,
					strerror(errno));
				if (selinux_enforce) {
					freecon(oldContext);
					return 1;
				}
			}
			freecon(oldContext);
	    } else {
		if (errno != ENOENT && errno != ENOTSUP) {
			message(MESS_ERROR, "getting file context %s: %s\n",
				log->files[logNum], strerror(errno));
			if (selinux_enforce) {
				return 1;
			}
		}
	    }
	}
#endif

    /* First compress the previous log when necessary */
    if (log->flags & LOG_FLAG_COMPRESS &&
	log->flags & LOG_FLAG_DELAYCOMPRESS) {
	if (log->flags & LOG_FLAG_DATEEXT) {
		/* glob for uncompressed files with our pattern */
		if (asprintf(&glob_pattern, "%s/%s%s%s", rotNames->dirName,
					rotNames->baseName, dext_pattern, fileext) < 0) {
			message(MESS_FATAL, "could not allocate glob pattern memory\n");
		}
	    rc = glob(glob_pattern, 0, globerr, &globResult);
	    if (!rc && globResult.gl_pathc > 0) {
		sortGlobResult(&globResult, strlen(rotNames->dirName) + 1 + strlen(rotNames->baseName), dformat);
		for (i = 0; i < globResult.gl_pathc && !hasErrors; i++) {
		    struct stat sbprev;

			if (asprintf(&oldName, "%s", (globResult.gl_pathv)[i]) < 0) {
				message(MESS_FATAL, "could not allocate glob result memory\n");
			}
			if (stat(oldName, &sbprev)) {
			message(MESS_DEBUG,
				"previous log %s does not exist\n",
				oldName);
		    } else {
			hasErrors = compressLogFile(oldName, log, &sbprev);
		    }
		    free(oldName);
		}
	    } else {
		message(MESS_DEBUG,
			"glob finding logs to compress failed\n");
		/* fallback to old behaviour */
		if (asprintf(&oldName, "%s/%s.%d%s", rotNames->dirName,
			rotNames->baseName, logStart, fileext) < 0) {
				message(MESS_FATAL, "could not allocate oldName memory\n");
			}
		free(oldName);
	    }
	    globfree(&globResult);
	    free(glob_pattern);
	} else {
	    struct stat sbprev;
	    if (asprintf(&oldName, "%s/%s.%d%s", rotNames->dirName,
		    rotNames->baseName, logStart, fileext) < 0) {
			message(MESS_FATAL, "could not allocate oldName memory\n");
	    }
	    if (stat(oldName, &sbprev)) {
		message(MESS_DEBUG, "previous log %s does not exist\n",
			oldName);
	    } else {
		hasErrors = compressLogFile(oldName, log, &sbprev);
	    }
	    free(oldName);
	}
    }

    /* adding 2 due to / and \0 being added by snprintf */
    rotNames->firstRotated =
	malloc(strlen(rotNames->dirName) + strlen(rotNames->baseName) +
	       strlen(fileext) + strlen(compext) + DATEEXT_LEN + 2 );

    if (log->flags & LOG_FLAG_DATEEXT) {
	/* glob for compressed files with our pattern
	 * and compress ext */
	if (asprintf(&glob_pattern, "%s/%s%s%s%s", rotNames->dirName,
				rotNames->baseName, dext_pattern, fileext, compext) < 0) {
		message(MESS_ERROR, "could not allocate glob pattern memory\n");
	}
	rc = glob(glob_pattern, 0, globerr, &globResult);
	if (!rc) {
	    /* search for files to drop, if we find one remember it,
	     * if we find another one mail and remove the first and
	     * remember the second and so on */
	    struct stat fst_buf;
	    int mail_out = -1;
	    /* remove the first (n - rotateCount) matches
	     * no real rotation needed, since the files have
	     * the date in their name */
		sortGlobResult(&globResult, strlen(rotNames->dirName) + 1 + strlen(rotNames->baseName), dformat);
	    for (i = 0; i < globResult.gl_pathc; i++) {
		if (!stat((globResult.gl_pathv)[i], &fst_buf)) {
		    if ((i <= ((int) globResult.gl_pathc - rotateCount))
			|| ((log->rotateAge > 0)
			    &&
			    (((nowSecs - fst_buf.st_mtime) / DAY_SECONDS)
			     > log->rotateAge))) {
			if (mail_out != -1) {
			    char *mailFilename =
				(globResult.gl_pathv)[mail_out];
			    if (!hasErrors && log->logAddress)
				hasErrors =
				    mailLogWrapper(mailFilename,
						   mailCommand, logNum,
						   log);
			    if (!hasErrors) {
				message(MESS_DEBUG, "removing %s\n", mailFilename);
				hasErrors = removeLogFile(mailFilename, log);
				}
			}
			mail_out = i;
		    }
		}
	    }
	    if (mail_out != -1) {
		/* oldName is oldest Backup found (for unlink later) */
		if (asprintf(&oldName, "%s", (globResult.gl_pathv)[mail_out]) < 0) {
		    message(MESS_FATAL, "could not allocate mailout memory\n");
		}
		rotNames->disposeName = malloc(strlen(oldName)+1);
		strcpy(rotNames->disposeName, oldName);
		free(oldName);
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
	sprintf(rotNames->firstRotated, "%s/%s%s%s%s",
		rotNames->dirName, rotNames->baseName, dext_str, fileext,
		(log->flags & LOG_FLAG_DELAYCOMPRESS) ? "" : compext);
	globfree(&globResult);
	free(glob_pattern);
    } else {
	if (log->rotateAge) {
	    struct stat fst_buf;
	    for (i = 1; i <= rotateCount + 1; i++) {
		if (asprintf(&oldName, "%s/%s.%d%s%s", rotNames->dirName,
			rotNames->baseName, i, fileext, compext) < 0) {
		    message(MESS_FATAL, "could not allocate mailFilename memory\n");
		}
		if (!stat(oldName, &fst_buf)
		    && (((nowSecs - fst_buf.st_mtime) / DAY_SECONDS)
			> log->rotateAge)) {
		    char *mailFilename = oldName;
		    if (!hasErrors && log->logAddress)
			hasErrors =
			    mailLogWrapper(mailFilename, mailCommand,
					   logNum, log);
		    if (!hasErrors)
			hasErrors = removeLogFile(mailFilename, log);
		}
		free(oldName);
	    }
	}

	if (asprintf(&oldName, "%s/%s.%d%s%s", rotNames->dirName,
		rotNames->baseName, logStart + rotateCount, fileext,
		compext) < 0) {
	    message(MESS_FATAL, "could not allocate disposeName memory\n");
	}
	newName = strdup(oldName);

	rotNames->disposeName = strdup(oldName);

	sprintf(rotNames->firstRotated, "%s/%s.%d%s%s", rotNames->dirName,
		rotNames->baseName, logStart, fileext,
		(log->flags & LOG_FLAG_DELAYCOMPRESS) ? "" : compext);

	for (i = rotateCount + logStart - 1; (i >= 0) && !hasErrors; i--) {
		free(newName);
		newName = oldName;
		if (asprintf(&oldName, "%s/%s.%d%s%s", rotNames->dirName,
		    rotNames->baseName, i, fileext, compext) < 0) {
		    message(MESS_FATAL, "could not allocate oldName memory\n");
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
	    if (hasErrors || i - 1 < 0)
		    free(oldName);
	    
	}
	free(newName);
    }				/* !LOG_FLAG_DATEEXT */

	if (log->flags & LOG_FLAG_DATEEXT) {
		char *destFile;
		struct stat fst_buf;

		if (asprintf(&(rotNames->finalName), "%s/%s%s%s", rotNames->dirName,
					rotNames->baseName, dext_str, fileext) < 0) {
			message(MESS_FATAL, "could not allocate finalName memory\n");
		}
		if (asprintf(&destFile, "%s%s", rotNames->finalName, compext) < 0) {
			message(MESS_FATAL, "could not allocate destFile memory\n");
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
			message(MESS_ERROR, "could not allocate finalName memory\n");
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

int rotateSingleLog(struct logInfo *log, int logNum, struct logState *state,
		    struct logNames *rotNames)
{
    int hasErrors = 0;
    struct stat sb;
    int fd;
#ifdef WITH_SELINUX
    security_context_t savedContext = NULL;
#endif
    char *tmpFilename = NULL;

    if (!state->doRotate)
	return 0;

    if (!hasErrors) {

	if (!(log->flags & (LOG_FLAG_COPYTRUNCATE | LOG_FLAG_COPY))) {
#ifdef WITH_SELINUX
		if (selinux_enabled) {
			security_context_t oldContext = NULL;
			int fdcurr = -1;

			if ((fdcurr = open(log->files[logNum], O_RDWR | O_NOFOLLOW)) < 0) {
				message(MESS_ERROR, "error opening %s: %s\n",
						log->files[logNum],
					strerror(errno));
				return 1;
			}
			if (fgetfilecon_raw(fdcurr, &oldContext) >= 0) {
				if (getfscreatecon_raw(&savedContext) < 0) {
					message(MESS_ERROR,
						"getting default context: %s\n",
						strerror(errno));
					if (selinux_enforce) {
						freecon(oldContext);
						if (close(fdcurr) < 0)
							message(MESS_ERROR, "error closing file %s",
									log->files[logNum]);
						return 1;
					}
				}
				if (setfscreatecon_raw(oldContext) < 0) {
					message(MESS_ERROR,
						"setting file context %s to %s: %s\n",
						log->files[logNum], oldContext, strerror(errno));
					if (selinux_enforce) {
						freecon(oldContext);
						if (close(fdcurr) < 0)
							message(MESS_ERROR, "error closing file %s",
									log->files[logNum]);
						return 1;
					}
				}
				message(MESS_DEBUG, "fscreate context set to %s\n",
						oldContext);
				freecon(oldContext);
			} else {
				if (errno != ENOTSUP) {
					message(MESS_ERROR, "getting file context %s: %s\n",
						log->files[logNum], strerror(errno));
					if (selinux_enforce) {
						if (close(fdcurr) < 0)
							message(MESS_ERROR, "error closing file %s",
									log->files[logNum]);
						return 1;
					}
				}
			}
			if (close(fdcurr) < 0)
				message(MESS_ERROR, "error closing file %s",
						log->files[logNum]);
		}
#endif
#ifdef WITH_ACL
		if ((prev_acl = acl_get_file(log->files[logNum], ACL_TYPE_ACCESS)) == NULL) {
			if (!ACL_NOT_WELL_SUPPORTED(errno)) {
				message(MESS_ERROR, "getting file ACL %s: %s\n",
					log->files[logNum], strerror(errno));
				hasErrors = 1;
			}
		}
#endif /* WITH_ACL */
		if (log->flags & LOG_FLAG_TMPFILENAME) {
			if (asprintf(&tmpFilename, "%s%s", log->files[logNum], ".tmp") < 0) {
				message(MESS_FATAL, "could not allocate tmpFilename memory\n");
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
		}
		else {
			message(MESS_DEBUG, "renaming %s to %s\n", log->files[logNum],
				rotNames->finalName);
			if (!debug && !hasErrors &&
			rename(log->files[logNum], rotNames->finalName)) {
				message(MESS_ERROR, "failed to rename %s to %s: %s\n",
					log->files[logNum], tmpFilename,
					strerror(errno));
					hasErrors = 1;
			}
	    }

	    if (!log->rotateCount) {
		rotNames->disposeName =
		    realloc(rotNames->disposeName,
			    strlen(rotNames->dirName) +
			    strlen(rotNames->baseName) +
			    strlen(log->files[logNum]) + 10);
		sprintf(rotNames->disposeName, "%s%s", rotNames->finalName,
			(log->compress_ext
			 && (log->flags & LOG_FLAG_COMPRESS)) ?
			log->compress_ext : "");
		message(MESS_DEBUG, "disposeName will be %s\n",
			rotNames->disposeName);
	    }
	}

	if (!hasErrors && log->flags & LOG_FLAG_CREATE &&
	    !(log->flags & (LOG_FLAG_COPYTRUNCATE | LOG_FLAG_COPY))) {
	    if (log->createUid == NO_UID)
		sb.st_uid = state->sb.st_uid;
	    else
		sb.st_uid = log->createUid;

	    if (log->createGid == NO_GID)
		sb.st_gid = state->sb.st_gid;
	    else
		sb.st_gid = log->createGid;

	    int have_create_mode = 0;
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
			fd = createOutputFile(log->files[logNum], O_CREAT | O_RDWR,
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
#ifdef WITH_SELINUX
	if (selinux_enabled) {
	    setfscreatecon_raw(savedContext);
		freecon(savedContext);
		savedContext = NULL;
	}
#endif

	if (!hasErrors
	    && log->flags & (LOG_FLAG_COPYTRUNCATE | LOG_FLAG_COPY)
		&& !(log->flags & LOG_FLAG_TMPFILENAME)) {
	    hasErrors =
		copyTruncate(log->files[logNum], rotNames->finalName,
			     &state->sb, log->flags);
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

int postrotateSingleLog(struct logInfo *log, int logNum, struct logState *state,
			struct logNames *rotNames)
{
    int hasErrors = 0;

    if (!state->doRotate) {
	return 0;
    }

    if (!hasErrors && log->flags & LOG_FLAG_TMPFILENAME) {
		char *tmpFilename = NULL;
		if (asprintf(&tmpFilename, "%s%s", log->files[logNum], ".tmp") < 0) {
			message(MESS_FATAL, "could not allocate tmpFilename memory\n");
			return 1;
		}
	    hasErrors =
		copyTruncate(tmpFilename, rotNames->finalName,
			     &state->sb, LOG_FLAG_COPY);
		message(MESS_DEBUG, "removing tmp log %s \n", tmpFilename);
		if (!debug && !hasErrors) {
			unlink(tmpFilename);
		}
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
	    hasErrors =
		mailLogWrapper(mailFilename, mailCommand, logNum, log);
    }

    if (!hasErrors && rotNames->disposeName)
	hasErrors = removeLogFile(rotNames->disposeName, log);

#ifdef WITH_SELINUX
    if (selinux_enabled) {
		setfscreatecon_raw(prev_context);
		freecon(prev_context);
		prev_context = NULL;
	}
#endif
    return hasErrors;
}

int rotateLogSet(struct logInfo *log, int force)
{
    int i, j;
    int hasErrors = 0;
    int logHasErrors[log->numFiles];
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
        message(MESS_DEBUG, "after %llu days ", log->threshhold);
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
        message(MESS_DEBUG, "%llu bytes ", log->threshhold);
        break;
        }
    }

    if (log->rotateCount)
	message(MESS_DEBUG, "(%d rotations)\n", log->rotateCount);
    else
	message(MESS_DEBUG, "(no old logs will be kept)\n");

    if (log->oldDir)
	message(MESS_DEBUG, "olddir is %s, ", log->oldDir);

    if (log->flags & LOG_FLAG_IFEMPTY)
	message(MESS_DEBUG, "empty log files are rotated, ");
    else
	message(MESS_DEBUG, "empty log files are not rotated, ");

    if (log->minsize) 
	message(MESS_DEBUG, "only log files >= %llu bytes are rotated, ",	log->minsize);

    if (log->maxsize) 
	message(MESS_DEBUG, "log files >= %llu are rotated earlier, ",	log->maxsize);

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

	if (log->flags & LOG_FLAG_SU) {
		if (switch_user(log->suUid, log->suGid) != 0) {
			return 1;
		}
	}

    for (i = 0; i < log->numFiles; i++) {
	logHasErrors[i] = findNeedRotating(log, i, force);
	hasErrors |= logHasErrors[i];

	/* sure is a lot of findStating going on .. */
	if ((findState(log->files[i]))->doRotate)
	    numRotated++;
    }

    if (log->first) {
	if (!numRotated) {
	    message(MESS_DEBUG, "not running first action script, "
		    "since no logs will be rotated\n");
	} else {
	    message(MESS_DEBUG, "running first action script\n");
	    if (runScript(log, log->pattern, log->first)) {
		message(MESS_ERROR, "error running first action script "
			"for %s\n", log->pattern);
		hasErrors = 1;
		if (log->flags & LOG_FLAG_SU) {
			if (switch_user_back() != 0) {
				return 1;
			}
		}
		/* finish early, firstaction failed, affects all logs in set */
		return hasErrors;
	    }
	}
    }

    state = malloc(log->numFiles * sizeof(struct logState *));
    rotNames = malloc(log->numFiles * sizeof(struct logNames *));

    for (j = 0;
	 (!(log->flags & LOG_FLAG_SHAREDSCRIPTS) && j < log->numFiles)
	 || ((log->flags & LOG_FLAG_SHAREDSCRIPTS) && j < 1); j++) {

	for (i = j;
	     ((log->flags & LOG_FLAG_SHAREDSCRIPTS) && i < log->numFiles)
	     || (!(log->flags & LOG_FLAG_SHAREDSCRIPTS) && i == j); i++) {
	    state[i] = findState(log->files[i]);

	    rotNames[i] = malloc(sizeof(struct logNames));
	    memset(rotNames[i], 0, sizeof(struct logNames));

	    logHasErrors[i] |=
		prerotateSingleLog(log, i, state[i], rotNames[i]);
	    hasErrors |= logHasErrors[i];
	}

	if (log->pre
		&& (!(
			((logHasErrors[j] || !state[j]->doRotate) && !(log->flags & LOG_FLAG_SHAREDSCRIPTS))
			|| (hasErrors && (log->flags & LOG_FLAG_SHAREDSCRIPTS))
		))
	) {
	    if (!numRotated) {
		message(MESS_DEBUG, "not running prerotate script, "
			"since no logs will be rotated\n");
	    } else {
		message(MESS_DEBUG, "running prerotate script\n");
		if (runScript(log, log->flags & LOG_FLAG_SHAREDSCRIPTS ? log->pattern : log->files[j], log->pre)) {
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
			((logHasErrors[j] || !state[j]->doRotate) && !(log->flags & LOG_FLAG_SHAREDSCRIPTS))
			|| (hasErrors && (log->flags & LOG_FLAG_SHAREDSCRIPTS))
		))
	) {
	    if (!numRotated) {
		message(MESS_DEBUG, "not running postrotate script, "
			"since no logs were rotated\n");
	    } else {
		message(MESS_DEBUG, "running postrotate script\n");
		if (runScript(log, log->flags & LOG_FLAG_SHAREDSCRIPTS ? log->pattern : log->files[j], log->post)) {
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
	    if (runScript(log, log->pattern, log->last)) {
		message(MESS_ERROR, "error running last action script "
			"for %s\n", log->pattern);
		hasErrors = 1;
	    }
	}
    }

	if (log->flags & LOG_FLAG_SU) {
		if (switch_user_back() != 0) {
			return 1;
		}
	}

    return hasErrors;
}

static int writeState(char *stateFilename)
{
	struct logState *p;
	FILE *f;
	char *chptr;
	int i;
	int error = 0;
	int bytes = 0;
	int fdcurr;
	int fdsave;
	struct stat sb;
	char *tmpFilename = NULL;
	struct tm now = *localtime(&nowSecs);
	time_t now_time, last_time;

	tmpFilename = malloc(strlen(stateFilename) + 5 );
	if (tmpFilename == NULL) {
		message(MESS_ERROR, "could not allocate memory for "
			"tmp state filename\n");
		return 1;
	}
	strcpy(tmpFilename, stateFilename);
	strcat(tmpFilename, ".tmp");
	/* Remove possible tmp state file from previous run */
	unlink(tmpFilename);


	if ((fdcurr = open(stateFilename, O_RDONLY)) < 0) {
	    message(MESS_ERROR, "error opening %s: %s\n", stateFilename,
		    strerror(errno));
		free(tmpFilename);
	    return 1;
	}

#ifdef WITH_SELINUX
	if (selinux_enabled) {
	    security_context_t oldContext;
	    if (fgetfilecon_raw(fdcurr, &oldContext) >= 0) {
		if (getfscreatecon_raw(&prev_context) < 0) {
		    message(MESS_ERROR,
			    "getting default context: %s\n",
			    strerror(errno));
		    if (selinux_enforce) {
				freecon(oldContext);
				free(tmpFilename);
				return 1;
		    }
		}
		if (setfscreatecon_raw(oldContext) < 0) {
		    message(MESS_ERROR,
			    "setting file context %s to %s: %s\n",
			    tmpFilename, oldContext, strerror(errno));
			if (selinux_enforce) {
				freecon(oldContext);
				free(tmpFilename);
				return 1;
		    }
		}
		message(MESS_DEBUG, "set default create context\n");
		freecon(oldContext);
	    } else {
		    if (errno != ENOTSUP) {
			    message(MESS_ERROR, "getting file context %s: %s\n",
				    tmpFilename, strerror(errno));
			    if (selinux_enforce) {
					free(tmpFilename);
				    return 1;
			    }
		    }
	    }
	}
#endif
#ifdef WITH_ACL
	if ((prev_acl = acl_get_fd(fdcurr)) == NULL) {
		if (!ACL_NOT_WELL_SUPPORTED(errno)) {
			message(MESS_ERROR, "getting file ACL %s: %s\n",
				stateFilename, strerror(errno));
			close(fdcurr);
			return 1;
		}
	}
#endif

	close(fdcurr);
	stat(stateFilename, &sb);

	fdsave = createOutputFile(tmpFilename, O_RDWR | O_CREAT | O_TRUNC, &sb, prev_acl, 0);
#ifdef WITH_ACL
	if (prev_acl) {
		acl_free(prev_acl);
		prev_acl = NULL;
	}
#endif
#ifdef WITH_SELINUX
	if (selinux_enabled) {
	    setfscreatecon_raw(prev_context);
		freecon(prev_context);
		prev_context = NULL;
	}
#endif

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
		error = fsync(fdsave);

	if (error == 0)
		error = fclose(f);
	else
		fclose(f);

	if (error == 0) {
		if (rename(tmpFilename, stateFilename)) {
			unlink(tmpFilename);
			error = 1;
			message(MESS_ERROR, "error renaming temp state file %s\n",
				tmpFilename);
		}
	}
	else {
		unlink(tmpFilename);
		if (errno)
			message(MESS_ERROR, "error creating temp state file %s: %s\n",
			tmpFilename, strerror(errno));
		else
			message(MESS_ERROR, "error creating temp state file %s%s\n",
				tmpFilename, error == ENOMEM ?
				": Insufficient storage space is available." : "" );
	}
	free(tmpFilename);
	return error;
}

static int readState(char *stateFilename)
{
    FILE *f;
    char buf[STATEFILE_BUFFER_SIZE];
    char *filename;
    const char **argv;
    int argc;
    int year, month, day, hour, minute, second;
    int i;
    int line = 0;
    int error;
    struct logState *st;
    time_t lr_time;
    struct stat f_stat;

    message(MESS_DEBUG, "Reading state from file: %s\n", stateFilename);

    error = stat(stateFilename, &f_stat);

	if ((error && errno == ENOENT) || (!error && f_stat.st_size == 0)) {
		/* create the file before continuing to ensure we have write
		access to the file */
		f = fopen(stateFilename, "w");
		if (!f) {
			message(MESS_ERROR, "error creating state file %s: %s\n",
				stateFilename, strerror(errno));
			return 1;
		}
		fprintf(f, "logrotate state -- version 2\n");
		fclose(f);

		if (allocateHash(64) != 0)
			return 1;

		return 0;
	} else if (error) {
		message(MESS_ERROR, "error stat()ing state file %s: %s\n",
			stateFilename, strerror(errno));
		return 1;
	}

    f = fopen(stateFilename, "r");
    if (!f) {
	message(MESS_ERROR, "error opening state file %s: %s\n",
		stateFilename, strerror(errno));
	return 1;
    }

    if (!fgets(buf, sizeof(buf) - 1, f)) {
	message(MESS_ERROR, "error reading top line of %s\n",
		stateFilename);
	fclose(f);
	return 1;
    }

    if (strcmp(buf, "logrotate state -- version 1\n") &&
	strcmp(buf, "logrotate state -- version 2\n")) {
	fclose(f);
	message(MESS_ERROR, "bad top line in state file %s\n",
		stateFilename);
	return 1;
    }

	/* Try to estimate how many state entries we have in the state file.
	 * We expect single entry to have around 80 characters (Of course this is
	 * just an estimation). During the testing I've found out that 200 entries
	 * per single hash entry gives good mem/performance ratio. */
	if (allocateHash(f_stat.st_size / 80 / 200) != 0)
		return 1;

    line++;

    while (fgets(buf, sizeof(buf) - 1, f)) {
	argv = NULL;
	line++;
	i = strlen(buf);
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

	year -= 1900, month -= 1;

	filename = strdup(argv[0]);
	unescape(filename);
	
	if ((st = findState(filename)) == NULL) {
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
	st->lastRotated = *localtime(&lr_time);

	free(argv);
	free(filename);
    }

    fclose(f);
    return 0;
}

int main(int argc, const char **argv)
{
    int force = 0;
    char *stateFile = STATEFILE;
    char *logFile = NULL;
    FILE *logFd = 0;
    int rc = 0;
    int arg;
    const char **files;
    poptContext optCon;
	struct logInfo *log;

    struct poptOption options[] = {
	{"debug", 'd', 0, 0, 'd',
	 "Don't do anything, just test (implies -v)"},
	{"force", 'f', 0, &force, 0, "Force file rotation"},
	{"mail", 'm', POPT_ARG_STRING, &mailCommand, 0,
	 "Command to send mail (instead of `" DEFAULT_MAIL_COMMAND "')",
	 "command"},
	{"state", 's', POPT_ARG_STRING, &stateFile, 0,
	 "Path of state file",
	 "statefile"},
	{"verbose", 'v', 0, 0, 'v', "Display messages during rotation"},
	{"log", 'l', POPT_ARG_STRING, &logFile, 'l', "Log file or 'syslog' to log to syslog"},
	{"version", '\0', POPT_ARG_NONE, NULL, 'V', "Display version information"},
	POPT_AUTOHELP {0, 0, 0, 0, 0}
    };

    logSetLevel(MESS_NORMAL);
    setlocale (LC_ALL, "");

    /* Creates the dir for state file and moves old
       state file to new location */
    mkdir(STATEDIR, 0755);
    rename(OLDSTATEFILE, STATEFILE);

    optCon = poptGetContext("logrotate", argc, argv, options, 0);
    poptReadDefaultConfig(optCon, 1);
    poptSetOtherOptionHelp(optCon, "[OPTION...] <configfile>");

    while ((arg = poptGetNextOpt(optCon)) >= 0) {
	switch (arg) {
	case 'd':
	    debug = 1;
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
	    fprintf(stderr, "logrotate %s\n", VERSION);
	    poptFreeContext(optCon);
	    exit(0);
	}
    }

    if (arg < -1) {
	fprintf(stderr, "logrotate: bad argument %s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));
	poptFreeContext(optCon);
	return 2;
    }

    files = poptGetArgs((poptContext) optCon);
    if (!files) {
	fprintf(stderr, "logrotate " VERSION
		" - Copyright (C) 1995-2001 Red Hat, Inc.\n");
	fprintf(stderr,
		"This may be freely redistributed under the terms of "
		"the GNU Public License\n\n");
	poptPrintUsage(optCon, stderr, 0);
	poptFreeContext(optCon);
	exit(1);
    }
#ifdef WITH_SELINUX
    selinux_enabled = (is_selinux_enabled() > 0);
    selinux_enforce = security_getenforce();
#endif

	TAILQ_INIT(&logs);

	if (readAllConfigPaths(files)) {
	poptFreeContext(optCon);
	exit(1);
    }

    poptFreeContext(optCon);
    nowSecs = time(NULL);

	if (readState(stateFile))
		exit(1);

	message(MESS_DEBUG, "\nHandling %d logs\n", numLogs);

	for (log = logs.tqh_first; log != NULL; log = log->list.tqe_next)
		rc |= rotateLogSet(log, force);

	if (!debug)
		rc |= writeState(stateFile);

	return (rc != 0);
}
