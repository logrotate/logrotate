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

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
static security_context_t prev_context=NULL;
int selinux_enabled=0;
#endif

#include "basenames.h"
#include "log.h"
#include "logrotate.h"

typedef struct {
    char * fn;
    struct tm lastRotated;	/* only tm.mon, tm_mday, tm_year are good! */
    struct stat sb;
    int doRotate;
} logState;

struct stateSet {
    logState * states;
    int numStates;
};

#define NO_MODE ((mode_t) -1)
#define NO_UID  ((uid_t) -1)
#define NO_GID  ((gid_t) -1)

int debug = 0;
char * mailCommand = DEFAULT_MAIL_COMMAND;
time_t nowSecs = 0;

static logState * findState(const char * fn, struct stateSet * sip) {
    int i;
    logState * states = sip->states;
    int numStates = sip->numStates;
    struct tm now = *localtime(&nowSecs);
    time_t lr_time;

    for (i = 0; i < numStates; i++) 
	if (!strcmp(fn, states[i].fn)) break;

    if (i == numStates) {
	i = numStates++;
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

static int runScript(char * logfn, char * script) {
    int fd;
    char *filespec;
    int rc;
    char buf[256];

    if (debug) {
	message(MESS_DEBUG, "running script with arg %s: \"%s\"\n", 
		logfn, script);
	return 0;
    }

    filespec = buf;
    snprintf(buf, sizeof(buf), "%s/logrotate.XXXXXX", getenv("TMPDIR") ?: "/tmp");
    fd = -1;
    if (!filespec || (fd = mkstemp(filespec)) < 0 || fchmod(fd, 0700)) {
	message(MESS_DEBUG, "error creating %s: %s\n", filespec,
		strerror(errno));
	if (fd >= 0) {
	    close(fd);
	    unlink(filespec);
	}
	return -1;
    }

    if (write(fd, "#!/bin/sh\n\n", 11) != 11 ||
	write(fd, script, strlen(script)) != strlen(script)) {
	message(MESS_DEBUG, "error writing %s\n", filespec);
	close(fd);
	unlink(filespec);
	return -1;
    }

    close(fd);

    if (!fork()) {
	execlp(filespec, filespec, logfn, NULL);
	exit(1);
    }

    wait(&rc);

    unlink(filespec);

    return rc;
}

static int compressLogFile(char * name, logInfo * log, struct stat *sb) {
    char * compressedName;
    const char ** fullCommand;
    int inFile;
    int outFile;
    int i;
    int status;

    fullCommand = alloca(sizeof(*fullCommand) * 
			 (log->compress_options_count + 2));
    fullCommand[0] = log->compress_prog;
    for (i = 0; i < log->compress_options_count; i++)
	fullCommand[i + 1] = log->compress_options_list[i];
    fullCommand[log->compress_options_count + 1] = NULL;
    
    compressedName = alloca(strlen(name) + 
			    strlen(log->compress_ext) + 2);
    sprintf(compressedName, "%s%s", name, log->compress_ext);

    if ((inFile = open(name, O_RDONLY)) < 0) {
	message(MESS_ERROR, "unable to open %s for compression\n", name);
	return 1;
    }
    
    if ((outFile = open(compressedName, O_RDWR | O_CREAT | O_TRUNC, sb->st_mode)) < 0) {
	message(MESS_ERROR, "unable to open %s for compressed output\n", 
		compressedName);
	close(inFile);
	return 1;
    }
    if (fchmod(outFile, (S_IRUSR | S_IWUSR) & sb->st_mode)) {
	message(MESS_ERROR, "error setting mode of %s: %s\n",
		compressedName, strerror(errno));
	close(outFile);
	close(inFile);
	return 1;
    }
    if (fchown(outFile, sb->st_uid, sb->st_gid)) {
	message(MESS_ERROR, "error setting owner of %s: %s\n",
		compressedName, strerror(errno));
	close(outFile);
	close(inFile);
	return 1;
    }
    if (fchmod(outFile, sb->st_mode)) {
	message(MESS_ERROR, "error setting mode of %s: %s\n",
		compressedName, strerror(errno));
	close(outFile);
	close(inFile);
	return 1;
    }

    message(MESS_DEBUG, "compressing log with: %s\n", fullCommand[0]);

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

    unlink(name);

    return 0;
}

static int mailLog(char * logFile, char * mailCommand, char * uncompressCommand, 
		   char * address, char * subject) {
    int mailInput;
    pid_t mailChild, uncompressChild;
    int mailStatus, uncompressStatus;
    int uncompressPipe[2];
    char * mailArgv[] = { mailCommand, "-s", subject, address, NULL };
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

static int copyTruncate(char * currLog, char * saveLog, struct stat * sb, int flags) {
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
	if ((selinux_enabled=(is_selinux_enabled()>0)))
	  {
	    security_context_t oldContext;
	    if (fgetfilecon(fdcurr, &oldContext) >=0) {
	      if (getfscreatecon(&prev_context) < 0) {
		message(MESS_ERROR, "error getting default context: %s\n", 
			strerror(errno));
		freecon(oldContext);
		return 1;
	      }
	      if (setfscreatecon(oldContext) < 0) {
		message(MESS_ERROR, "error setting file context %s to %s: %s\n", 
			saveLog, oldContext,strerror(errno));
		freecon(oldContext);
		return 1;
	      }
	      freecon(oldContext);
	    } else {
	      message(MESS_ERROR, "error getting file context %s: %s\n", currLog,
		      strerror(errno));
	      return 1;
	    }
	  }
#endif
	fdsave = open(saveLog, O_WRONLY | O_CREAT | O_TRUNC,sb->st_mode);
#ifdef WITH_SELINUX
	if (selinux_enabled) {
	  setfscreatecon(prev_context);
	  if (prev_context!= NULL) {
	    freecon(prev_context);
	    prev_context=NULL;
	  }
	}
#endif
	if (fdsave < 0) {
	    message(MESS_ERROR, "error creating %s: %s\n", saveLog,
		strerror(errno));
	    close(fdcurr);
	    return 1;
	}
	if (fchmod(fdsave, (S_IRUSR | S_IWUSR) & sb->st_mode)) {
	    message(MESS_ERROR, "error setting mode of %s: %s\n",
		saveLog, strerror(errno));
	    close(fdcurr);
	    close(fdsave);
	    return 1;
	}
	if (fchown(fdsave, sb->st_uid, sb->st_gid)) {
	    message(MESS_ERROR, "error setting owner of %s: %s\n",
		saveLog, strerror(errno));
	    close(fdcurr);
	    close(fdsave);
	    return 1;
	}
	if (fchmod(fdsave, sb->st_mode)) {
	    message(MESS_ERROR, "error setting mode of %s: %s\n",
		saveLog, strerror(errno));
	    close(fdcurr);
	    close(fdsave);
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

        if (!debug) {
	    if (ftruncate(fdcurr, 0)) {
	        message(MESS_ERROR, "error truncating %s: %s\n", currLog,
		    strerror(errno));
	        close(fdcurr);
	        close(fdsave);
	        return 1;
	    } else {
	        close(fdcurr);
	        close(fdsave);
	    }
        }
    } else {
        message(MESS_DEBUG, "Not truncating %s\n", currLog);
        close(fdcurr);
        close(fdsave);
    }

    return 0;
}

int findNeedRotating(logInfo * log, int logNum, struct stateSet * sip) {
    struct stat sb;
    logState * state = NULL;
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
            state->doRotate = ((now.tm_wday < state->lastRotated.tm_wday) ||
			       ((mktime(&now) - mktime(&state->lastRotated)) >
				(7 * 24 * 3600)));
            break;
          case ROT_MONTHLY:
            /* rotate if the logs haven't been rotated this month or
               this year */
	      state->doRotate = ((now.tm_mon != state->lastRotated.tm_mon) ||
				 (now.tm_year != state->lastRotated.tm_year));
            break;
          case ROT_DAYS:
            /* FIXME: only days=1 is implemented!! */
	      state->doRotate = 1;
	      break;
	default:
            /* ack! */
            state->doRotate = 0;
            break;
        }
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

int rotateSingleLog(logInfo * log, int logNum, logState * state) {
    struct tm now = *localtime(&nowSecs);
    char * oldName, * newName = NULL;
    char * disposeName;
    char * finalName;
    char * tmp;
    char * compext = "";
    char * fileext = "";
    int hasErrors = 0;
    int i;
    int fd;
    uid_t createUid;
    gid_t createGid;
    mode_t createMode;
    char * baseName;
    char * dirName;
    char * firstRotated;
    size_t alloc_size;
    int rotateCount = log->rotateCount ? log->rotateCount : 1;
    int logStart = (log->logStart == -1) ? 1 : log->logStart;

    if (!state->doRotate) return 0;

    /* Logs with rotateCounts of 0 are rotated once, then removed. This
       lets scripts run properly, and everything gets mailed properly. */

    message(MESS_DEBUG, "rotating log %s, log->rotateCount is %d\n", log->files[logNum], log->rotateCount);
    
    if (log->flags & LOG_FLAG_COMPRESS) compext = log->compress_ext;
    
    state->lastRotated = now;
    
    if (log->oldDir) {
	if (log->oldDir[0] != '/') {
	    char *ld = ourDirName(log->files[logNum]);
	    dirName = malloc(strlen(ld) + strlen(log->oldDir) + 2);
	    sprintf(dirName, "%s/%s", ld, log->oldDir);
	    free(ld);
	} else
	  dirName = strdup(log->oldDir);
    } else
        dirName = ourDirName(log->files[logNum]);

    baseName = strdup(ourBaseName(log->files[logNum]));

    alloc_size = strlen(dirName) + strlen(baseName) + 
                 strlen(log->files[logNum]) + strlen(fileext) +
                 strlen(compext) + 10;
    
    oldName = alloca(alloc_size);
    newName = alloca(alloc_size);
    disposeName = alloca(alloc_size);
    
    if (log->extension &&
	strncmp(&baseName[strlen(baseName)-strlen(log->extension)],
		log->extension, strlen(log->extension)) == 0) {
        char *tempstr;
	
        fileext = log->extension;
        tempstr = calloc(strlen(baseName)-strlen(log->extension)+1, sizeof(char));
        strncat(tempstr, baseName,
                strlen(baseName)-strlen(log->extension));
        free(baseName);
        baseName = tempstr;
    }
    
    /* First compress the previous log when necessary */
    if (log->flags & LOG_FLAG_COMPRESS &&
        log->flags & LOG_FLAG_DELAYCOMPRESS) {
        struct stat sbprev;
	
        sprintf(oldName, "%s/%s.%d%s", dirName, baseName, logStart, fileext);
        if (stat(oldName, &sbprev)) {
            message(MESS_DEBUG, "previous log %s does not exist\n",
		    oldName);
        } else {
	    hasErrors = compressLogFile(oldName, log, &sbprev);
	}
    }
    
    sprintf(oldName, "%s/%s.%d%s%s", dirName, baseName,
            logStart + rotateCount, fileext, compext);
    strcpy(newName, oldName);
    
    strcpy(disposeName, oldName);
    
    firstRotated = alloca(strlen(dirName) + strlen(baseName) +
                          strlen(fileext) + strlen(compext) + 30);
    sprintf(firstRotated, "%s/%s.%d%s%s", dirName, baseName,
            logStart, fileext, 
	    (log->flags & LOG_FLAG_DELAYCOMPRESS) ? "" : compext);
    
#ifdef WITH_SELINUX
    if ((selinux_enabled=(is_selinux_enabled()>0))) {
      security_context_t oldContext=NULL;
      if (getfilecon(log->files[logNum], &oldContext)>0) {
	if (getfscreatecon(&prev_context) < 0) {
	  message(MESS_ERROR, "error getting default context: %s\n", 
		  strerror(errno));
	  freecon(oldContext);
	  return 1;
	}
	if (setfscreatecon(oldContext) < 0) {
	  message(MESS_ERROR, "error setting file context %s to %s: %s\n", 
		  log->files[logNum], oldContext,strerror(errno));
	  freecon(oldContext);
	  return 1;
	}
	freecon(oldContext);
      } else {
	message(MESS_ERROR, "error getting file context %s: %s\n", 
		log->files[logNum], 
		strerror(errno));
	return 1;
      }
    }
#endif
    for (i = rotateCount + logStart - 1; (i >= 0) && !hasErrors; i--) {
        tmp = newName;
        newName = oldName;
        oldName = tmp;
        sprintf(oldName, "%s/%s.%d%s%s", dirName, baseName, i,
                fileext, compext);
	
        message(MESS_DEBUG, "renaming %s to %s (rotatecount %d, logstart %d, i %d), \n", oldName, newName,
		rotateCount, logStart, i);
	
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
    
    finalName = oldName;
    
    /* note: the gzip extension is *not* used here! */
    sprintf(finalName, "%s/%s.%d%s", dirName, baseName, logStart, fileext);
    
    /* if the last rotation doesn't exist, that's okay */
    if (!debug && access(disposeName, F_OK)) {
        message(MESS_DEBUG, "log %s doesn't exist -- won't try to "
                "dispose of it\n", disposeName);
        disposeName = NULL;
    } 
    
    free(dirName);
    free(baseName);
    
    if (!hasErrors) {
        if (log->pre && !(log->flags & LOG_FLAG_SHAREDSCRIPTS)) {
            message(MESS_DEBUG, "running prerotate script\n");
            if (runScript(log->files[logNum], log->pre)) {
                message(MESS_ERROR, "error running prerotate script, "
			"leaving old log in place\n");
                hasErrors = 1;
	    }
        }
	
        if (!(log->flags & (LOG_FLAG_COPYTRUNCATE|LOG_FLAG_COPY))) {
            message(MESS_DEBUG, "renaming %s to %s\n", log->files[logNum], 
		    finalName);
	    
            if (!debug && !hasErrors &&
		rename(log->files[logNum], finalName)) {
                message(MESS_ERROR, "failed to rename %s to %s: %s\n",
			log->files[logNum], finalName, strerror(errno));
	    }

	    if (!log->rotateCount) {
	      disposeName = alloca(strlen(dirName) + strlen(baseName) + 
				   strlen(log->files[logNum]) + 10);
	      sprintf(disposeName, "%s%s", finalName, (log->compress_ext && (log->flags & LOG_FLAG_COMPRESS))?log->compress_ext:"");
	      message(MESS_DEBUG, "disposeName will be %s\n", disposeName);
	    }
        }
	
        if (!hasErrors && log->flags & LOG_FLAG_CREATE &&
            !(log->flags & (LOG_FLAG_COPYTRUNCATE|LOG_FLAG_COPY))) {
            if (log->createUid == NO_UID)
                createUid = state->sb.st_uid;
            else
                createUid = log->createUid;
	    
            if (log->createGid == NO_GID)
                createGid = state->sb.st_gid;
            else
                createGid = log->createGid;
	    
            if (log->createMode == NO_MODE)
                createMode = state->sb.st_mode & 0777;
            else
                createMode = log->createMode;
	    
            message(MESS_DEBUG, "creating new log mode = 0%o uid = %d "
                    "gid = %d\n", (unsigned int)createMode, (int)createUid, (int)createGid);
	    
            if (!debug) {
                fd = open(log->files[logNum], O_CREAT | O_RDWR, createMode);
                if (fd < 0) {
                    message(MESS_ERROR, "error creating %s: %s\n", 
                            log->files[logNum], strerror(errno));
                    hasErrors = 1;
                } else {
                    if (fchmod(fd, (S_IRUSR | S_IWUSR) & createMode)) {
                        message(MESS_ERROR, "error setting mode of "
                                "%s: %s\n", log->files[logNum], 
				strerror(errno));
                        hasErrors = 1;
                    }
                    if (fchown(fd, createUid, createGid)) {
                        message(MESS_ERROR, "error setting owner of "
                                "%s: %s\n", log->files[logNum], 
				strerror(errno));
                        hasErrors = 1;
		    }
	            if (fchmod(fd, createMode)) {
                        message(MESS_ERROR, "error setting mode of "
                                "%s: %s\n", log->files[logNum], 
				strerror(errno));
                        hasErrors = 1;
                    }

                    close(fd);
		}
	    }
        }
	
        if (!hasErrors && log->flags & (LOG_FLAG_COPYTRUNCATE|LOG_FLAG_COPY))
            hasErrors = copyTruncate(log->files[logNum], finalName,
                                     &state->sb, log->flags);
	
        if (!hasErrors && log->post && 
	    !(log->flags & LOG_FLAG_SHAREDSCRIPTS)) {
            message(MESS_DEBUG, "running postrotate script\n");
            if (runScript(log->files[logNum], log->post)) {
                message(MESS_ERROR, "error running postrotate script\n");
                hasErrors = 1;
	    }
        }

        if (!hasErrors && 
	    (log->flags & LOG_FLAG_COMPRESS) &&
	    !(log->flags & LOG_FLAG_DELAYCOMPRESS)) {
	    hasErrors = compressLogFile(finalName, log, &state->sb);
	}
	
        if (!hasErrors && log->logAddress) {
            char * mailFilename;
	    
            if (log->flags & LOG_FLAG_MAILFIRST)
                mailFilename = firstRotated;
            else
                mailFilename = disposeName;

            if (mailFilename) {
		/* if the log is compressed (and we're not mailing a
		   file whose compression has been delayed), we need
		   to uncompress it */
                if ((log->flags & LOG_FLAG_COMPRESS) &&
		    !((log->flags & LOG_FLAG_DELAYCOMPRESS) &&
		      (log->flags & LOG_FLAG_MAILFIRST))) {
		    if (mailLog(mailFilename, mailCommand, 
				log->uncompress_prog, log->logAddress, 
				log->files[logNum])) 
			hasErrors = 1;
		} else {
		    if (mailLog(mailFilename, mailCommand, NULL, 
			        log->logAddress, mailFilename))
			hasErrors = 1;
		}
	    }
        }
	
        if (!hasErrors && disposeName) {
            message(MESS_DEBUG, "removing old log %s\n", disposeName);
	    
            if (!debug && unlink(disposeName)) {
                message(MESS_ERROR, "Failed to remove old log %s: %s\n",
			disposeName, strerror(errno));
                hasErrors = 1;
	    }
	}
    }
    
#ifdef WITH_SELINUX
	if (selinux_enabled) {
	  setfscreatecon(prev_context);
	  if (prev_context!= NULL) {
	    freecon(prev_context);
	    prev_context=NULL;
	  }
	}
#endif
    return hasErrors;
}

int rotateLogSet(logInfo * log, struct stateSet * sip, int force) {
    int i;
    int hasErrors = 0;
    int numRotated = 0;
    logState * state;

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
    
    if (log->logAddress) {
	message(MESS_DEBUG, "old logs mailed to %s\n", log->logAddress);
    } else {
	message(MESS_DEBUG, "old logs are removed\n");
    }
    
    for (i = 0; i < log->numFiles; i++) {
        hasErrors |= findNeedRotating(log, i, sip);
	
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
            }
	}
    }

    if (log->pre && (log->flags & LOG_FLAG_SHAREDSCRIPTS)) {
        if (!numRotated) {
            message(MESS_DEBUG, "not running shared prerotate script, "
                    "since no logs will be rotated\n");
        } else {
            message(MESS_DEBUG, "running shared prerotate script\n");
            if (runScript(log->pattern, log->pre)) {
                message(MESS_ERROR, "error running shared prerotate script "
                        "for %s\n", log->pattern);
                hasErrors = 1;
            }
	}
    }
    
    /* should there be an if(!hasErrors) here? */
    for (i = 0; i < log->numFiles; i++) {
	state = findState(log->files[i], sip);
	
	hasErrors |= rotateSingleLog(log, i, state);
    }
    
    if (log->post && (log->flags & LOG_FLAG_SHAREDSCRIPTS)) {
        if (!numRotated) {
            message(MESS_DEBUG, "not running shared postrotate script, "
                    "since no logs were rotated\n");
        } else {
            message(MESS_DEBUG, "running shared postrotate script\n");
            if (runScript(log->pattern, log->post)) {
                message(MESS_ERROR, "error running shared postrotate script "
                        "for %s\n", log->pattern);
                hasErrors = 1;
            }
	}
    }
    
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

static int writeState(char * stateFilename, struct stateSet si) {
    FILE * f;
    char * chptr;
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

static int readState(char * stateFilename, struct stateSet * sip) {
    FILE * f;
    char buf[1024];
    const char ** argv;
    int argc;
    int year, month, day;
    int i;
    int line = 0;
    int error;
    logState * st;
    time_t lr_time;
    struct stat f_stat;

    error = stat(stateFilename, &f_stat);

    if ((error && errno == ENOENT) ||
	(!error && f_stat.st_size == 0)) {
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
	message(MESS_ERROR, "error reading top line of %s\n", stateFilename);
	fclose(f);
	return 1;
    }
    
    if (strcmp(buf, "logrotate state -- version 1\n") &&
	strcmp(buf, "logrotate state -- version 2\n")) {
	fclose(f);
	message(MESS_ERROR, "bad top line in state file %s\n", stateFilename);
	return 1;
    }
    
    line++;
    
    while (fgets(buf, sizeof(buf) - 1, f)) {
	line++;
	i = strlen(buf);
	if (buf[i - 1] != '\n') {
	    message(MESS_ERROR, "line too long in state file %s\n", 
		    stateFilename);
	    fclose(f);
	    return 1;
	}
	
	buf[i - 1] = '\0';
	
	if (i == 1) continue;
	
	if (poptParseArgvString(buf, &argc, &argv) || (argc != 2) ||
	    (sscanf(argv[1], "%d-%d-%d", &year, &month, &day) != 3)) {
	    message(MESS_ERROR, "bad line %d in state file %s\n", 
		    line, stateFilename);
	    fclose(f);
	    return 1;
	}
	
	/* Hack to hide earlier bug */
	if ((year != 1900) && (year < 1996 || year > 2100)) {
	    message(MESS_ERROR, "bad year %d for file %s in state file %s\n",
		    year, argv[0], stateFilename);
	    fclose(f);
	    return 1;
	}
	
	if (month < 1 || month > 12) {
	    message(MESS_ERROR, "bad month %d for file %s in state file %s\n",
		    month, argv[0], stateFilename);
	    fclose(f);
	    return 1;
	}
	
	/* 0 to hide earlier bug */
	if (day < 0 || day > 31) {
	    message(MESS_ERROR, "bad day %d for file %s in state file %s\n",
		    day, argv[0], stateFilename);
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

int main(int argc, const char ** argv) {
    logInfo defConfig = { NULL, NULL, 0, NULL, ROT_SIZE, 
			  /* threshHold */ 1024 * 1024, 0,
			  /* log start */ -1,
			  /* pre, post */ NULL, NULL,
			  /* first, last */ NULL, NULL,
			  /* logAddress */ NULL, 
			  /* extension */ NULL, 
			  /* compression */ COMPRESS_COMMAND,
			  UNCOMPRESS_COMMAND, COMPRESS_EXT,
			  /* rotate pattern */ NULL,
			  /* flags */ LOG_FLAG_IFEMPTY,
			  /* createMode */ NO_MODE, NO_UID, NO_GID };
    int numLogs = 0;
    int force = 0;
    logInfo * logs = NULL;
    struct stateSet si = { NULL, 0 };
    char * stateFile = STATEFILE;
    int i;
    int rc = 0;
    int arg;
    const char ** files, ** file;
    poptContext optCon;
    struct poptOption options[] = {
	{ "debug", 'd', 0, 0, 'd', 
	  "Don't do anything, just test (implies -v)" },
	{ "force", 'f', 0 , &force, 0, "Force file rotation" },
	{ "mail", 'm', POPT_ARG_STRING, &mailCommand, 0, 
	  "Command to send mail (instead of `" DEFAULT_MAIL_COMMAND "')",
	  "command" },
	{ "state", 's', POPT_ARG_STRING, &stateFile, 0, "Path of state file",
	  "statefile" },
	{ "verbose", 'v', 0, 0, 'v', "Display messages during rotation" },
	POPT_AUTOHELP
	{ 0, 0, 0, 0, 0 } 
    };
    
    logSetLevel(MESS_NORMAL);

    optCon = poptGetContext("logrotate", argc, argv, options,0);
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
	return 2;
    }

    files = poptGetArgs((poptContext) optCon);
    if (!files) {
	fprintf(stderr, "logrotate " VERSION 
		" - Copyright (C) 1995-2001 Red Hat, Inc.\n");
	fprintf(stderr, "This may be freely redistributed under the terms of "
		"the GNU Public License\n\n");
	poptPrintUsage(optCon, stderr, 0);
	exit(1);
    }

    for (file = files; *file; file++) {
	if (readConfigPath(*file, &defConfig, &logs, &numLogs)) {
	    exit(1);
	}
    }

    nowSecs = time(NULL);

    if (readState(stateFile, &si)) {
	exit(1);
    }

    message(MESS_DEBUG, "\nHandling %d logs\n", numLogs);

    for (i = 0; i < numLogs; i++) {
	rc |= rotateLogSet(logs + i, &si, force);
    }

    if (!debug && writeState(stateFile, si)) {
	exit(1);
    }

    return (rc != 0);
}
