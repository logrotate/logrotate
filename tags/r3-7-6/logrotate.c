#include <alloca.h>
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

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
static security_context_t prev_context = NULL;
int selinux_enabled = 0;
int selinux_enforce = 0;
#endif

#include "basenames.h"
#include "log.h"
#include "logrotate.h"

#if !defined(GLOB_ABORTED) && defined(GLOB_ABEND)
#define GLOB_ABORTED GLOB_ABEND
#endif

typedef struct {
    char *fn;
    struct tm lastRotated;	/* only tm.mon, tm_mday, tm_year are good! */
    struct stat sb;
    int doRotate;
} logState;

typedef struct {
    char *firstRotated;
    char *disposeName;
    char *finalName;
    char *dirName;
    char *baseName;
} logNames;

struct stateSet {
    logState *states;
    int numStates;
};

int debug = 0;
char *mailCommand = DEFAULT_MAIL_COMMAND;
time_t nowSecs = 0;

static int shred_file(char * filename, logInfo *log);

static int globerr(const char *pathname, int theerr)
{
    message(MESS_ERROR, "error accessing %s: %s\n", pathname,
	    strerror(theerr));

    /* We want the glob operation to continue, so return 0 */
    return 1;
}

/* logInfo instances may share dynamic allocated memory, so to avoid
 * free memory already free'd, it is necessary check if a item in a
 * logInfo instance points to the same memory address of the item in
 * previous instances, which were free'd before. */

#define freeLogItem(what) \
    if (log->what) { \
        for (j = 0; j < i; j++) \
            if (log->what == (*logsPtr + j)->what) \
                break; \
        if (j == i) \
            free(log->what); \
    }

static void free_logInfo(logInfo ** logsPtr, int *numLogsPtr)
{
    int i, j;
    logInfo *log;

    for (i = 0; i < *numLogsPtr; i++) {
	log = *logsPtr + i;
	freeLogItem(pattern);
	freeLogItem(oldDir);
	freeLogItem(pre);
	freeLogItem(post);
	freeLogItem(first);
	freeLogItem(last);
	freeLogItem(logAddress);
	freeLogItem(extension);
	freeLogItem(compress_prog);
	freeLogItem(uncompress_prog);
	freeLogItem(compress_ext);
	freeLogItem(compress_options_list);

	for (j = 0; j < log->numFiles; j++)
	    free(log->files[j]);
	free(log->files);
    }
    free(*logsPtr);
    *numLogsPtr = 0;
}

static logState *findState(const char *fn, struct stateSet *sip)
{
    int i;
    logState *states = sip->states;
    int numStates = sip->numStates;
    struct tm now = *localtime(&nowSecs);
    time_t lr_time;

    for (i = 0; i < numStates; i++)
	if (!strcmp(fn, states[i].fn))
	    break;

    if (i == numStates) {
	numStates++;
	states = realloc(states, sizeof(*states) * numStates);
	states[i].fn = strdup(fn);
	memset(&states[i].lastRotated, 0, sizeof(states[i].lastRotated));
	states[i].doRotate = 0;

	states[i].lastRotated.tm_mon = now.tm_mon;
	states[i].lastRotated.tm_mday = now.tm_mday;
	states[i].lastRotated.tm_year = now.tm_year;

	/* fill in the rest of the st->lastRotated fields */
	lr_time = mktime(&states[i].lastRotated);
	states[i].lastRotated = *localtime(&lr_time);

	sip->states = states;
	sip->numStates = numStates;
    }

    return (states + i);
}

static int runScript(char *logfn, char *script)
{
    int rc;

    if (debug) {
	message(MESS_DEBUG, "running script with arg %s: \"%s\"\n",
		logfn, script);
	return 0;
    }

    if (!fork()) {
	execl("/bin/sh", "sh", "-c", script, script, logfn, NULL);
	exit(1);
    }

    wait(&rc);
    return rc;
}

int createOutputFile(char *fileName, int flags, struct stat *sb)
{
    int fd;

    fd = open(fileName, flags, sb->st_mode);
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
    if (fchown(fd, sb->st_uid, sb->st_gid)) {
	message(MESS_ERROR, "error setting owner of %s: %s\n",
		fileName, strerror(errno));
	close(fd);
	return -1;
    }
    if (fchmod(fd, sb->st_mode)) {
	message(MESS_ERROR, "error setting mode of %s: %s\n",
		fileName, strerror(errno));
	close(fd);
	return -1;
    }
    return fd;
}

#define SHRED_CALL "shred -u "
#define SHRED_COUNT_FLAG "-n "
#define DIGITS 10
/* unlink, but try to call shred from GNU fileutils */
static int shred_file(char * filename, logInfo *log)
{
	int len, ret;
	char *cmd;
	char count[DIGITS];    /*  that's a lot of shredding :)  */

	if (!(log->flags & LOG_FLAG_SHRED)) {
		return unlink(filename);
	}

	len = strlen(filename) + strlen(SHRED_CALL);
	len += strlen(SHRED_COUNT_FLAG) + DIGITS;
	cmd = malloc(len);

	if (!cmd) {
		message(MESS_ERROR, "malloc error while shredding");
		return unlink(filename);
	}
	strcpy(cmd, SHRED_CALL);
	if (log->shred_cycles != 0) {
		strcat(cmd, SHRED_COUNT_FLAG);
		snprintf(count, DIGITS - 1, "%d", log->shred_cycles);
		strcat(count, " ");
		strcat(cmd, count);
	}
	strcat(cmd, filename);
	ret = system(cmd);
	free(cmd);
	if (ret != 0) {
		message(MESS_ERROR, "Failed to shred %s\n, trying unlink", filename);
		if (ret != -1) {
			message(MESS_NORMAL, "Shred returned %d\n", ret);
		}
		return unlink(filename);
	} else {
		return ret;
	}
}

static int removeLogFile(char *name, logInfo *log)
{
    message(MESS_DEBUG, "removing old log %s\n", name);

    if (!debug && shred_file(name, log)) {
	message(MESS_ERROR, "Failed to remove old log %s: %s\n",
		name, strerror(errno));
	return 1;
    }
    return 0;
}

static int compressLogFile(char *name, logInfo * log, struct stat *sb)
{
    char *compressedName;
    const char **fullCommand;
    int inFile;
    int outFile;
    int i;
    int status;

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

    if ((inFile = open(name, O_RDONLY)) < 0) {
	message(MESS_ERROR, "unable to open %s for compression\n", name);
	return 1;
    }

    outFile =
	createOutputFile(compressedName, O_RDWR | O_CREAT | O_TRUNC, sb);
    if (outFile < 0) {
	close(inFile);
	return 1;
    }

    if (!fork()) {
	dup2(inFile, 0);
	close(inFile);
	dup2(outFile, 1);
	close(outFile);

	execvp(fullCommand[0], (void *) fullCommand);
	exit(1);
    }

    close(inFile);
    close(outFile);

    wait(&status);

    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	message(MESS_ERROR, "failed to compress log %s\n", name);
	return 1;
    }

    shred_file(name, log);

    return 0;
}

static int mailLog(char *logFile, char *mailCommand,
		   char *uncompressCommand, char *address, char *subject)
{
    int mailInput;
    pid_t mailChild, uncompressChild;
    int mailStatus, uncompressStatus;
    int uncompressPipe[2];
    char *mailArgv[] = { mailCommand, "-s", subject, address, NULL };
    int rc = 0;

    if ((mailInput = open(logFile, O_RDONLY)) < 0) {
	message(MESS_ERROR, "failed to open %s for mailing: %s\n", logFile,
		strerror(errno));
	return 1;
    }

    if (uncompressCommand) {
	pipe(uncompressPipe);
	if (!(uncompressChild = fork())) {
	    /* uncompress child */
	    dup2(mailInput, 0);
	    close(mailInput);
	    dup2(uncompressPipe[1], 1);
	    close(uncompressPipe[0]);
	    close(uncompressPipe[1]);

	    execlp(uncompressCommand, uncompressCommand, NULL);
	    exit(1);
	}

	close(mailInput);
	mailInput = uncompressPipe[0];
	close(uncompressPipe[1]);
    }

    if (!(mailChild = fork())) {
	dup2(mailInput, 0);
	close(mailInput);
	close(1);

	execvp(mailArgv[0], mailArgv);
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

static int mailLogWrapper(char *mailFilename, char *mailCommand,
			  int logNum, logInfo * log)
{
    /* if the log is compressed (and we're not mailing a
     * file whose compression has been delayed), we need
     * to uncompress it */
    if ((log->flags & LOG_FLAG_COMPRESS) &&
	!((log->flags & LOG_FLAG_DELAYCOMPRESS) &&
	  (log->flags & LOG_FLAG_MAILFIRST))) {
	if (mailLog(mailFilename, mailCommand,
		    log->uncompress_prog, log->logAddress,
		    log->files[logNum]))
	    return 1;
    } else {
	if (mailLog(mailFilename, mailCommand, NULL,
		    log->logAddress, mailFilename))
	    return 1;
    }
    return 0;
}

static int copyTruncate(char *currLog, char *saveLog, struct stat *sb,
			int flags)
{
    char buf[BUFSIZ];
    int fdcurr = -1, fdsave = -1;
    ssize_t cnt;

    message(MESS_DEBUG, "copying %s to %s\n", currLog, saveLog);

    if (!debug) {
	if ((fdcurr = open(currLog, O_RDWR)) < 0) {
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
			return 1;
		    }
		}
		if (setfscreatecon_raw(oldContext) < 0) {
		    message(MESS_ERROR,
			    "setting file context %s to %s: %s\n",
			    saveLog, oldContext, strerror(errno));
		    if (selinux_enforce) {
			freecon(oldContext);
			return 1;
		    }
		}
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
	fdsave =
	    createOutputFile(saveLog, O_WRONLY | O_CREAT | O_TRUNC, sb);
#ifdef WITH_SELINUX
	if (selinux_enabled) {
	    setfscreatecon_raw(prev_context);
	    if (prev_context != NULL) {
		freecon(prev_context);
		prev_context = NULL;
	    }
	}
#endif
	if (fdsave < 0) {
	    close(fdcurr);
	    return 1;
	}
	while ((cnt = read(fdcurr, buf, sizeof(buf))) > 0) {
	    if (write(fdsave, buf, cnt) != cnt) {
		message(MESS_ERROR, "error writing to %s: %s\n",
			saveLog, strerror(errno));
		close(fdcurr);
		close(fdsave);
		return 1;
	    }
	}
	if (cnt != 0) {
	    message(MESS_ERROR, "error reading %s: %s\n",
		    currLog, strerror(errno));
	    close(fdcurr);
	    close(fdsave);
	    return 1;
	}
    }

    if (flags & LOG_FLAG_COPYTRUNCATE) {
	message(MESS_DEBUG, "truncating %s\n", currLog);

	if (!debug)
	    if (ftruncate(fdcurr, 0)) {
		message(MESS_ERROR, "error truncating %s: %s\n", currLog,
			strerror(errno));
		close(fdcurr);
		close(fdsave);
		return 1;
	    }
    } else
	message(MESS_DEBUG, "Not truncating %s\n", currLog);

    close(fdcurr);
    close(fdsave);
    return 0;
}

int findNeedRotating(logInfo * log, int logNum, struct stateSet *sip)
{
    struct stat sb;
    logState *state = NULL;
    struct tm now = *localtime(&nowSecs);

    message(MESS_DEBUG, "considering log %s\n", log->files[logNum]);

    if (stat(log->files[logNum], &sb)) {
	if ((log->flags & LOG_FLAG_MISSINGOK) && (errno == ENOENT)) {
	    message(MESS_DEBUG, "  log %s does not exist -- skipping\n",
		    log->files[logNum]);
	    return 0;
	}
	message(MESS_ERROR, "stat of %s failed: %s\n", log->files[logNum],
		strerror(errno));
	return 1;
    }

    state = findState(log->files[logNum], sip);
    state->doRotate = 0;
    state->sb = sb;

    if (log->criterium == ROT_SIZE) {
	state->doRotate = (sb.st_size >= log->threshhold);
    } else if (log->criterium == ROT_FORCE) {
	/* user forced rotation of logs from command line */
	state->doRotate = 1;
    } else if (state->lastRotated.tm_year > now.tm_year ||
	       (state->lastRotated.tm_year == now.tm_year &&
		(state->lastRotated.tm_mon > now.tm_mon ||
		 (state->lastRotated.tm_mon == now.tm_mon &&
		  state->lastRotated.tm_mday > now.tm_mday)))) {
	message(MESS_ERROR,
		"log %s last rotated in the future -- rotation forced\n",
		log->files[logNum]);
	state->doRotate = 1;
    } else if (state->lastRotated.tm_year != now.tm_year ||
	       state->lastRotated.tm_mon != now.tm_mon ||
	       state->lastRotated.tm_mday != now.tm_mday) {
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
	    break;
	case ROT_MONTHLY:
	    /* rotate if the logs haven't been rotated this month or
	       this year */
	    state->doRotate = ((now.tm_mon != state->lastRotated.tm_mon) ||
			       (now.tm_year !=
				state->lastRotated.tm_year));
	    break;
	case ROT_DAYS:
	    /* FIXME: only days=1 is implemented!! */
	    state->doRotate = 1;
	    break;
	case ROT_YEARLY:
	    /* rotate if the logs haven't been rotated this year */
	    state->doRotate = (now.tm_year != state->lastRotated.tm_year);
	    break;
	default:
	    /* ack! */
	    state->doRotate = 0;
	    break;
	}
	if (log->minsize && sb.st_size < log->minsize)
	    state->doRotate = 0;
    }

    /* The notifempty flag overrides the normal criteria */
    if (!(log->flags & LOG_FLAG_IFEMPTY) && !sb.st_size)
	state->doRotate = 0;

    if (state->doRotate) {
	message(MESS_DEBUG, "  log needs rotating\n");
    } else {
	message(MESS_DEBUG, "  log does not need rotating\n");
    }

    return 0;
}

int prerotateSingleLog(logInfo * log, int logNum, logState * state,
		       logNames * rotNames)
{
    struct tm now = *localtime(&nowSecs);
    char *oldName, *newName = NULL;
    char *tmp;
    char *compext = "";
    char *fileext = "";
    int hasErrors = 0;
    int i;
    char *glob_pattern;
    glob_t globResult;
    int rc;
    size_t alloc_size;
    int rotateCount = log->rotateCount ? log->rotateCount : 1;
    int logStart = (log->logStart == -1) ? 1 : log->logStart;

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

    alloc_size = strlen(rotNames->dirName) + strlen(rotNames->baseName) +
	strlen(log->files[logNum]) + strlen(fileext) +
	strlen(compext) + 18;

    oldName = alloca(alloc_size);
    newName = alloca(alloc_size);
    rotNames->disposeName = malloc(alloc_size);

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

    /* First compress the previous log when necessary */
    if (log->flags & LOG_FLAG_COMPRESS &&
	log->flags & LOG_FLAG_DELAYCOMPRESS) {
	if (log->flags & LOG_FLAG_DATEEXT) {
	    /* glob for uncompressed files with our pattern */
	    glob_pattern =
		malloc(strlen(rotNames->dirName) +
		       strlen(rotNames->baseName)
		       + strlen(fileext) + 44);
	    sprintf(glob_pattern,
		    "%s/%s-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]%s",
		    rotNames->dirName, rotNames->baseName, fileext);
	    rc = glob(glob_pattern, 0, globerr, &globResult);
	    if (!rc && globResult.gl_pathc > 0) {
		for (i = 0; i < globResult.gl_pathc && !hasErrors; i++) {
		    struct stat sbprev;

		    sprintf(oldName, "%s", (globResult.gl_pathv)[i]);
		    if (stat(oldName, &sbprev)) {
			message(MESS_DEBUG,
				"previous log %s does not exist\n",
				oldName);
		    } else {
			hasErrors = compressLogFile(oldName, log, &sbprev);
		    }
		}
	    } else {
		message(MESS_DEBUG,
			"glob finding logs to compress failed\n");
		/* fallback to old behaviour */
		sprintf(oldName, "%s/%s.%d%s", rotNames->dirName,
			rotNames->baseName, logStart, fileext);
	    }
	    globfree(&globResult);
	    free(glob_pattern);
	} else {
	    struct stat sbprev;

	    sprintf(oldName, "%s/%s.%d%s", rotNames->dirName,
		    rotNames->baseName, logStart, fileext);
	    if (stat(oldName, &sbprev)) {
		message(MESS_DEBUG, "previous log %s does not exist\n",
			oldName);
	    } else {
		hasErrors = compressLogFile(oldName, log, &sbprev);
	    }
	}
    }

    rotNames->firstRotated =
	malloc(strlen(rotNames->dirName) + strlen(rotNames->baseName) +
	       strlen(fileext) + strlen(compext) + 30);

    if (log->flags & LOG_FLAG_DATEEXT) {
	/* glob for compressed files with our pattern
	 * and compress ext */
	glob_pattern =
	    malloc(strlen(rotNames->dirName) + strlen(rotNames->baseName)
		   + strlen(fileext) + strlen(compext) + 44);
	sprintf(glob_pattern,
		"%s/%s-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]%s%s",
		rotNames->dirName, rotNames->baseName, fileext, compext);
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
	    for (i = 0; i < globResult.gl_pathc; i++) {
		if (!stat((globResult.gl_pathv)[i], &fst_buf)) {
		    if ((i <= ((int) globResult.gl_pathc - rotateCount))
			|| ((log->rotateAge > 0)
			    &&
			    (((nowSecs - fst_buf.st_mtime) / 60 / 60 / 24)
			     > log->rotateAge))) {
			if (mail_out != -1) {
			    char *mailFilename =
				(globResult.gl_pathv)[mail_out];
			    if (!hasErrors && log->logAddress)
				hasErrors =
				    mailLogWrapper(mailFilename,
						   mailCommand, logNum,
						   log);
			    if (!hasErrors)
				hasErrors = removeLogFile(mailFilename, log);
			}
			mail_out = i;
		    }
		}
	    }
	    if (mail_out != -1) {
		/* oldName is oldest Backup found (for unlink later) */
		sprintf(oldName, "%s", (globResult.gl_pathv)[mail_out]);
		strcpy(rotNames->disposeName, oldName);
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
	sprintf(rotNames->firstRotated, "%s/%s-%04d%02d%02d%s%s",
		rotNames->dirName, rotNames->baseName, now.tm_year + 1900,
		now.tm_mon + 1, now.tm_mday, fileext, compext);
	globfree(&globResult);
	free(glob_pattern);
    } else {
	if (log->rotateAge) {
	    struct stat fst_buf;
	    for (i = 1; i <= rotateCount; i++) {
		sprintf(oldName, "%s/%s.%d%s%s", rotNames->dirName,
			rotNames->baseName, i, fileext, compext);
		if (!stat(oldName, &fst_buf)
		    && (((nowSecs - fst_buf.st_mtime) / 60 / 60 / 24)
			> log->rotateAge)) {
		    char *mailFilename = oldName;
		    if (!hasErrors && log->logAddress)
			hasErrors =
			    mailLogWrapper(mailFilename, mailCommand,
					   logNum, log);
		    if (!hasErrors)
			hasErrors = removeLogFile(mailFilename, log);
		}
	    }
	}

	sprintf(oldName, "%s/%s.%d%s%s", rotNames->dirName,
		rotNames->baseName, logStart + rotateCount, fileext,
		compext);
	strcpy(newName, oldName);

	strcpy(rotNames->disposeName, oldName);

	sprintf(rotNames->firstRotated, "%s/%s.%d%s%s", rotNames->dirName,
		rotNames->baseName, logStart, fileext,
		(log->flags & LOG_FLAG_DELAYCOMPRESS) ? "" : compext);

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
	for (i = rotateCount + logStart - 1; (i >= 0) && !hasErrors; i--) {
	    tmp = newName;
	    newName = oldName;
	    oldName = tmp;
	    sprintf(oldName, "%s/%s.%d%s%s", rotNames->dirName,
		    rotNames->baseName, i, fileext, compext);

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
    }				/* !LOG_FLAG_DATEEXT */

    rotNames->finalName = malloc(alloc_size);

    if (log->flags & LOG_FLAG_DATEEXT) {
	char *destFile =
	    alloca(strlen(rotNames->dirName) + strlen(rotNames->baseName) +
		   strlen(fileext) + strlen(compext) + 30);
	struct stat fst_buf;
	sprintf(rotNames->finalName, "%s/%s-%04d%02d%02d%s",
		rotNames->dirName, rotNames->baseName, now.tm_year + 1900,
		now.tm_mon + 1, now.tm_mday, fileext);
	sprintf(destFile, "%s%s", rotNames->finalName, compext);
	if (!stat(destFile, &fst_buf)) {
	    message(MESS_DEBUG,
		    "destination %s already exists, skipping rotation\n",
		    rotNames->firstRotated);
	    hasErrors = 1;
	}
    } else {
	/* note: the gzip extension is *not* used here! */
	sprintf(rotNames->finalName, "%s/%s.%d%s", rotNames->dirName,
		rotNames->baseName, logStart, fileext);
    }

    /* if the last rotation doesn't exist, that's okay */
    if (!debug && rotNames->disposeName
	&& access(rotNames->disposeName, F_OK)) {
	message(MESS_DEBUG,
		"log %s doesn't exist -- won't try to " "dispose of it\n",
		rotNames->disposeName);
	free(rotNames->disposeName);
	rotNames->disposeName = NULL;
    }

    return hasErrors;
}

int rotateSingleLog(logInfo * log, int logNum, logState * state,
		    logNames * rotNames)
{
    int hasErrors = 0;
    struct stat sb;
    int fd;

    if (!state->doRotate)
	return 0;

    if (!hasErrors) {

	if (!(log->flags & (LOG_FLAG_COPYTRUNCATE | LOG_FLAG_COPY))) {
	    message(MESS_DEBUG, "renaming %s to %s\n", log->files[logNum],
		    rotNames->finalName);

	    if (!debug && !hasErrors &&
		rename(log->files[logNum], rotNames->finalName)) {
		message(MESS_ERROR, "failed to rename %s to %s: %s\n",
			log->files[logNum], rotNames->finalName,
			strerror(errno));
	    }

	    if (!log->rotateCount) {
		rotNames->disposeName =
		    realloc(rotNames->disposeName,
			    strlen(rotNames->dirName) +
			    strlen(rotNames->baseName) +
			    strlen(log->files[logNum]) + 10);
		sprintf(rotNames->disposeName, "%s%s", rotNames->finalName,
			(log->compress_ext
			 && (log->flags & LOG_FLAG_COMPRESS)) ? log->
			compress_ext : "");
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

	    if (log->createMode == NO_MODE)
		sb.st_mode = state->sb.st_mode & 0777;
	    else
		sb.st_mode = log->createMode;

	    message(MESS_DEBUG, "creating new log mode = 0%o uid = %d "
		    "gid = %d\n", (unsigned int) sb.st_mode,
		    (int) sb.st_uid, (int) sb.st_gid);

	    if (!debug) {
		fd = createOutputFile(log->files[logNum], O_CREAT | O_RDWR,
				      &sb);
		if (fd < 0)
		    hasErrors = 1;
                else
                    close(fd);
	    }
	}

	if (!hasErrors
	    && log->flags & (LOG_FLAG_COPYTRUNCATE | LOG_FLAG_COPY))
	    hasErrors =
		copyTruncate(log->files[logNum], rotNames->finalName,
			     &state->sb, log->flags);

    }
    return hasErrors;
}

int postrotateSingleLog(logInfo * log, int logNum, logState * state,
			logNames * rotNames)
{
    int hasErrors = 0;

    if (!state->doRotate)
	return 0;

    if ((log->flags & LOG_FLAG_COMPRESS) &&
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
	if (prev_context != NULL) {
	    freecon(prev_context);
	    prev_context = NULL;
	}
    }
#endif
    return hasErrors;
}

int rotateLogSet(logInfo * log, struct stateSet *sip, int force)
{
    int i, j;
    int hasErrors = 0;
    int logHasErrors[log->numFiles];
    int numRotated = 0;
    logState **state;
    logNames **rotNames;

    if (force)
	log->criterium = ROT_FORCE;

    message(MESS_DEBUG, "\nrotating pattern: %s ", log->pattern);
    switch (log->criterium) {
    case ROT_DAYS:
	message(MESS_DEBUG, "after %d days ", log->threshhold);
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
	message(MESS_DEBUG, "%d bytes ", log->threshhold);
	break;
    case ROT_FORCE:
	message(MESS_DEBUG, "forced from command line ");
	break;
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
	message(MESS_DEBUG, "only log files >= %d bytes are rotated, ",	log->minsize);

    if (log->logAddress) {
	message(MESS_DEBUG, "old logs mailed to %s\n", log->logAddress);
    } else {
	message(MESS_DEBUG, "old logs are removed\n");
    }

    for (i = 0; i < log->numFiles; i++) {
	logHasErrors[i] = findNeedRotating(log, i, sip);
	hasErrors |= logHasErrors[i];

	/* sure is a lot of findStating going on .. */
	if ((findState(log->files[i], sip))->doRotate)
	    numRotated++;
    }

    if (log->first) {
	if (!numRotated) {
	    message(MESS_DEBUG, "not running first action script, "
		    "since no logs will be rotated\n");
	} else {
	    message(MESS_DEBUG, "running first action script\n");
	    if (runScript(log->pattern, log->first)) {
		message(MESS_ERROR, "error running first action script "
			"for %s\n", log->pattern);
		hasErrors = 1;
		/* finish early, firstaction failed, affects all logs in set */
		return hasErrors;
	    }
	}
    }

    state = malloc(log->numFiles * sizeof(logState *));
    rotNames = malloc(log->numFiles * sizeof(logNames *));

    for (j = 0;
	 (!(log->flags & LOG_FLAG_SHAREDSCRIPTS) && j < log->numFiles)
	 || ((log->flags & LOG_FLAG_SHAREDSCRIPTS) && j < 1); j++) {

	for (i = j;
	     ((log->flags & LOG_FLAG_SHAREDSCRIPTS) && i < log->numFiles)
	     || (!(log->flags & LOG_FLAG_SHAREDSCRIPTS) && i == j); i++) {
	    state[i] = findState(log->files[i], sip);

	    rotNames[i] = malloc(sizeof(logNames));
	    memset(rotNames[i], 0, sizeof(logNames));

	    logHasErrors[i] |=
		prerotateSingleLog(log, i, state[i], rotNames[i]);
	    hasErrors |= logHasErrors[i];
	}

	if (log->pre
	    && (! ( (logHasErrors[j] && !(log->flags & LOG_FLAG_SHAREDSCRIPTS))
		   || (hasErrors && (log->flags & LOG_FLAG_SHAREDSCRIPTS)) ) )) {
	    if (!numRotated) {
		message(MESS_DEBUG, "not running prerotate script, "
			"since no logs will be rotated\n");
	    } else {
		message(MESS_DEBUG, "running prerotate script\n");
		if (runScript(log->pattern, log->pre)) {
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
	    && (! ( (logHasErrors[j] && !(log->flags & LOG_FLAG_SHAREDSCRIPTS))
		   || (hasErrors && (log->flags & LOG_FLAG_SHAREDSCRIPTS)) ) )) {
	    if (!numRotated) {
		message(MESS_DEBUG, "not running postrotate script, "
			"since no logs were rotated\n");
	    } else {
		message(MESS_DEBUG, "running postrotate script\n");
		if (runScript(log->pattern, log->post)) {
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
	    if (runScript(log->pattern, log->last)) {
		message(MESS_ERROR, "error running last action script "
			"for %s\n", log->pattern);
		hasErrors = 1;
	    }
	}
    }

    return hasErrors;
}

static int writeState(char *stateFilename, struct stateSet si)
{
    FILE *f;
    char *chptr;
    int i;

    f = fopen(stateFilename, "w");

    if (!f) {
	message(MESS_ERROR, "error creating state file %s: %s\n",
		stateFilename, strerror(errno));
	return 1;
    }

    fprintf(f, "logrotate state -- version 2\n");

    for (i = 0; i < si.numStates; i++) {
	fputc('"', f);
	for (chptr = si.states[i].fn; *chptr; chptr++) {
	    switch (*chptr) {
	    case '"':
		fputc('\\', f);
	    }

	    fputc(*chptr, f);
	}

	fputc('"', f);
	fprintf(f, " %d-%d-%d\n",
		si.states[i].lastRotated.tm_year + 1900,
		si.states[i].lastRotated.tm_mon + 1,
		si.states[i].lastRotated.tm_mday);
    }

    fclose(f);

    return 0;
}

static int readState(char *stateFilename, struct stateSet *sip)
{
    FILE *f;
    char buf[1024];
    const char **argv;
    int argc;
    int year, month, day;
    int i;
    int line = 0;
    int error;
    logState *st;
    time_t lr_time;
    struct stat f_stat;

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

    line++;

    while (fgets(buf, sizeof(buf) - 1, f)) {
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

	if (poptParseArgvString(buf, &argc, &argv) || (argc != 2) ||
	    (sscanf(argv[1], "%d-%d-%d", &year, &month, &day) != 3)) {
	    message(MESS_ERROR, "bad line %d in state file %s\n",
		    line, stateFilename);
	    if (argv)
		free(argv);
	    fclose(f);
	    return 1;
	}

	/* Hack to hide earlier bug */
	if ((year != 1900) && (year < 1996 || year > 2100)) {
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

	year -= 1900, month -= 1;

	st = findState(argv[0], sip);

	st->lastRotated.tm_mon = month;
	st->lastRotated.tm_mday = day;
	st->lastRotated.tm_year = year;

	/* fill in the rest of the st->lastRotated fields */
	lr_time = mktime(&st->lastRotated);
	st->lastRotated = *localtime(&lr_time);

	free(argv);
    }

    fclose(f);

    return 0;

}

int main(int argc, const char **argv)
{
    int numLogs = 0;
    int force = 0;
    logInfo *logs = NULL;
    struct stateSet si = { NULL, 0 };
    char *stateFile = STATEFILE;
    int i;
    int rc = 0;
    int arg;
    const char **files;
    poptContext optCon;
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
	POPT_AUTOHELP {0, 0, 0, 0, 0}
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
	    /* fallthrough */
	case 'v':
	    logSetLevel(MESS_DEBUG);
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

    if (readAllConfigPaths(files, &logs, &numLogs)) {
	poptFreeContext(optCon);
	free_logInfo(&logs, &numLogs);
	exit(1);
    }

    poptFreeContext(optCon);
    nowSecs = time(NULL);

    if (readState(stateFile, &si)) {
	exit(1);
    }

    message(MESS_DEBUG, "\nHandling %d logs\n", numLogs);

    for (i = 0; i < numLogs; i++) {
	rc |= rotateLogSet(logs + i, &si, force);
    }

    if (!debug)
	rc |= writeState(stateFile, si);

    for (i = 0; i < si.numStates; i++) {
	free(si.states[i].fn);
    }
    free(si.states);

    free_logInfo(&logs, &numLogs);
    return (rc != 0);
}
