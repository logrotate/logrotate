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
#include <time.h>
#include <unistd.h>

#include "basenames.h"
#include "log.h"
#include "logrotate.h"

typedef struct {
    char * fn;
    struct tm lastRotated;	/* only tm.mon, tm_mday, tm_year are good! */
} logState;

#define NO_MODE ((mode_t) -1)
#define NO_UID  ((uid_t) -1)
#define NO_GID  ((gid_t) -1)

int debug = 0;
char * mailCommand = DEFAULT_MAIL_COMMAND;

static logState * findState(const char * fn, logState ** statesPtr, 
			    int * numStatesPtr) {
    int i;
    logState * states = *statesPtr;
    int numStates = *numStatesPtr;
    time_t nowSecs = time(NULL);
    struct tm now = *localtime(&nowSecs);
    time_t lr_time;

    for (i = 0; i < numStates; i++) 
	if (!strcmp(fn, states[i].fn)) break;

    if (i == numStates) {
	i = numStates++;
	states = realloc(states, sizeof(*states) * numStates);
	states[i].fn = strdup(fn);
	memset(&states[i].lastRotated, 0, sizeof(states[i].lastRotated));

	states[i].lastRotated.tm_mon = now.tm_mon;
	states[i].lastRotated.tm_mday = now.tm_mday;
	states[i].lastRotated.tm_year = now.tm_year;

	/* fill in the rest of the st->lastRotated fields */
	lr_time = mktime(&states[i].lastRotated);
	states[i].lastRotated = *localtime(&lr_time);

	*statesPtr = states;
	*numStatesPtr = numStates;
    }

    return (states + i);
}

static int runScript(char * logfn, char * script) {
    int fd;
    char filespec[32];
    char * cmd;
    int rc;

    if (debug) {
	message(MESS_DEBUG, "running script with arg %s: \"%s\"\n", 
		logfn, script);
	return 0;
    }

    strcpy(filespec, "/tmp/logrotate.XXXXXX");
    if ((fd = mkstemp(filespec)) < 0 || fchmod(fd, 0700)) {
	message(MESS_DEBUG, "error creating %s: %s\n", filespec,
		strerror(errno));
	if (fd >= 0) close(fd);
	return -1;
    }

    if (write(fd, "#!/bin/sh\n\n", 11) != 11) {
	close(fd);
	unlink(filespec);
	return -1;
    }
    if (write(fd, script, strlen(script)) != strlen(script)) {
	close(fd);
	unlink(filespec);
	return -1;
    }

    close(fd);

    cmd = alloca(strlen(filespec) + strlen(logfn) + 20);
    sprintf(cmd, "/bin/sh %s '%s'", filespec, logfn);
    rc = system(cmd);

    unlink(filespec);

    return rc;
}

static int copyTruncate(char * currLog, char * saveLog, struct stat * sb) {
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
	if ((fdsave = open(saveLog, O_WRONLY | O_CREAT | O_TRUNC,
		sb->st_mode)) < 0) {
	    message(MESS_ERROR, "error creating %s: %s\n", saveLog,
		strerror(errno));
	    close(fdcurr);
	    return 1;
	}
	if (fchmod(fdsave, sb->st_mode)) {
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

    return 0;
}

int rotateSingleLog(logInfo * log, int logNum, logState ** statesPtr, 
		    int * numStatesPtr, FILE * errorFile) {
    struct stat sb;
    time_t nowSecs = time(NULL);
    struct tm now = *localtime(&nowSecs);
    char * oldName, * newName = NULL;
    char * disposeName;
    char * finalName;
    char * tmp;
    char * compext = "";
    char * fileext = "";
    int hasErrors = 0;
    int doRotate = 0;
    int i;
    int fd;
    logState * state = NULL;
    uid_t createUid;
    gid_t createGid;
    mode_t createMode;
    char * baseName;
    char * dirName;
    char * firstRotated;
    int rotateCount = log->rotateCount ? log->rotateCount : 1;

    /* Logs with rotateCounts of 0 are rotated to .1, then removed. This
       lets scripts run properly, and everything gets mailed properly. */

    message(MESS_DEBUG, "rotating file %s\n", log->files[logNum]);

    if (log->flags & LOG_FLAG_COMPRESS) compext = log->compress_ext;
    
    if (stat(log->files[logNum], &sb)) {
	if ((log->flags & LOG_FLAG_MISSINGOK) && (errno == ENOENT)) {
	    message(MESS_DEBUG, "file %s does not exist -- skipping\n", 
		    log->files[logNum]);
	    return 0;
	}
	fprintf(errorFile, "stat of %s failed: %s\n", log->files[logNum],
		strerror(errno));
	hasErrors = 1;
    }

    if (!hasErrors) {
	state = findState(log->files[logNum], statesPtr, numStatesPtr);

	if (log->criterium == ROT_SIZE) {
	    doRotate = (sb.st_size >= log->threshhold);
	}
	else if (log->criterium == ROT_FORCE) {
	    /* user forced rotation of logs from command line */
	    doRotate = 1;
	} else if (state->lastRotated.tm_year > now.tm_year || 
		      (state->lastRotated.tm_year == now.tm_year && 
			  (state->lastRotated.tm_mon > now.tm_mon ||
			      (state->lastRotated.tm_mon == now.tm_mon &&
			       state->lastRotated.tm_mday > now.tm_mday)
			  )
		      )
		  ) {
	    message(MESS_ERROR,
	    "file %s last rotated in the future -- rotation forced\n",
	    log->files[logNum]);
	    doRotate = 1;
	} else if (state->lastRotated.tm_year != now.tm_year || 
		   state->lastRotated.tm_mon != now.tm_mon ||
		   state->lastRotated.tm_mday != now.tm_mday) {
	    switch (log->criterium) {
	      case ROT_WEEKLY:
		/* rotate if:
		      1) the current weekday is before the weekday of the
			 last rotation
		      2) more then a week has passed since the last
			 rotation 
		*/
		doRotate = ((now.tm_wday < state->lastRotated.tm_wday) ||
			    ((mktime(&now) - mktime(&state->lastRotated)) >
			       (7 * 24 * 3600)));
		break;
	      case ROT_MONTHLY:
		/* rotate if the logs haven't been rotated this month or
		   this year */
		doRotate = ((now.tm_mon != state->lastRotated.tm_mon) ||
			    (now.tm_year != state->lastRotated.tm_year));
		break;
	      case ROT_DAYS:
		/* FIXME: only days=1 is implemented!! */
		doRotate = 1;
		break;
	      default:
		/* ack! */
		doRotate = 0;
		break;
	    }
	} else {
	    doRotate = 0;
	}
    }

    /* The notifempty flag overrides the normal criteria */
    if (!(log->flags & LOG_FLAG_IFEMPTY) && !sb.st_size)
	doRotate = 0;

    if (!hasErrors && doRotate) {
	message(MESS_DEBUG, "log needs rotating\n");

	state->lastRotated = now;

	if (log->oldDir)
	    dirName = strdup(log->oldDir);
	else
	    dirName = ourDirName(log->files[logNum]);
	baseName = strdup(ourBaseName(log->files[logNum]));

	oldName = alloca(strlen(dirName) + strlen(baseName) + 
			    strlen(log->files[logNum]) + 10);
	newName = alloca(strlen(dirName) + strlen(baseName) + 
			    strlen(log->files[logNum]) + 10);
	disposeName = alloca(strlen(dirName) + strlen(baseName) + 
			    strlen(log->files[logNum]) + 10);


	if (log->extension)
	    fileext = log->extension;

	if (log->extension &&
		strncmp(&baseName[strlen(baseName)-strlen(log->extension)],
		log->extension, strlen(log->extension)) == 0) {
	    char *tempstr;
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

	    sprintf(oldName, "%s/%s.1%s", dirName, baseName, fileext);
	    if (stat(oldName, &sbprev)) {
		message(MESS_DEBUG, "previous log %s does not exist\n",
				    oldName);
	    } else {
		char * command;

		command = alloca(strlen(oldName) +
				    strlen(log->compress_prog) + 1 + strlen(log->compress_options) + 20);
		sprintf(command, "%s %s '%s'", log->compress_prog, log->compress_options, oldName);
		message(MESS_DEBUG, "compressing previous log with: %s\n",
				    command);
		if (!debug && system(command)) {
		    fprintf(errorFile,
			"failed to compress previous log %s\n", oldName);
		    hasErrors = 1;
		}
	    }
	}

	sprintf(oldName, "%s/%s.%d%s%s", dirName, baseName,
		rotateCount + 1, fileext, compext);

	strcpy(disposeName, oldName);

	firstRotated = alloca(strlen(dirName) + strlen(baseName) +
			      strlen(fileext) + strlen(compext) + 30);
	sprintf(firstRotated, "%s/%s.1%s%s", dirName, baseName,
		fileext, compext);

	for (i = rotateCount; i && !hasErrors; i--) {
	    tmp = newName;
	    newName = oldName;
	    oldName = tmp;
	    sprintf(oldName, "%s/%s.%d%s%s", dirName, baseName, i,
		    fileext, compext);

	    message(MESS_DEBUG, "renaming %s to %s\n", oldName, newName);

	    if (!debug && rename(oldName, newName)) {
		if (errno == ENOENT) {
		    message(MESS_DEBUG, "old log %s does not exist\n",
			    oldName);
		} else {
		    fprintf(errorFile, "error renaming %s to %s: %s\n",
			    oldName, newName, strerror(errno));
		    hasErrors = 1;
		}
	    }
	}

	finalName = oldName;

	/* note: the gzip extension is *not* used here! */
	sprintf(finalName, "%s/%s.1%s", dirName, baseName, fileext);

	/* if the last rotation doesn't exist, that's okay */
	if (!debug && access(disposeName, F_OK)) {
	    message(MESS_DEBUG, "file %s doesn't exist -- won't try "
		    "dispose of it\n", disposeName);
	    disposeName = NULL;
	} 

	free(dirName);
	free(baseName);

	if (!hasErrors) {
	    if (log->pre && !(log->flags & LOG_FLAG_SHAREDSCRIPTS)) {
		message(MESS_DEBUG, "running prerotate script\n");
		if (runScript(log->files[logNum], log->pre)) {
		    fprintf(errorFile, "error running prerotate script -- 
				leaving old log in place\n");
		    hasErrors = 1;
		}
	    }

	    if (!(log->flags & LOG_FLAG_COPYTRUNCATE)) {
		message(MESS_DEBUG, "renaming %s to %s\n", log->files[logNum], 
		    finalName);

		if (!debug && !hasErrors &&
			rename(log->files[logNum], finalName)) {
		    fprintf(errorFile, "failed to rename %s to %s: %s\n",
			log->files[logNum], finalName, strerror(errno));
		}
	    }

	    if (!hasErrors && log->flags & LOG_FLAG_CREATE &&
			!(log->flags & LOG_FLAG_COPYTRUNCATE)) {
		if (log->createUid == NO_UID)
		    createUid = sb.st_uid;
		else
		    createUid = log->createUid;
	    
		if (log->createGid == NO_GID)
		    createGid = sb.st_gid;
		else
		    createGid = log->createGid;
	    
		if (log->createMode == NO_MODE)
		    createMode = sb.st_mode & 0777;
		else
		    createMode = log->createMode;
	    
		message(MESS_DEBUG, "creating new log mode = 0%o uid = %d "
			"gid = %d\n", createMode, createUid, createGid);

		if (!debug) {
		    fd = open(log->files[logNum], O_CREAT | O_RDWR, createMode);
		    if (fd < 0) {
			message(MESS_ERROR, "error creating %s: %s\n", 
				log->files[logNum], strerror(errno));
			hasErrors = 1;
		    } else {
			if (fchmod(fd, createMode)) {
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

			close(fd);
		    }
		}
	    }

	    if (!hasErrors && log->flags & LOG_FLAG_COPYTRUNCATE) {
		hasErrors = copyTruncate(log->files[logNum], finalName, &sb);

	    }

	    if (!hasErrors && log->post && 
		    !(log->flags & LOG_FLAG_SHAREDSCRIPTS)) {
		message(MESS_DEBUG, "running postrotate script\n");
		if (runScript(log->files[logNum], log->post)) {
		    fprintf(errorFile, "error running postrotate script\n");
		    hasErrors = 1;
		}
	    }

	    if (!hasErrors && !log->rotateCount) {
		message(MESS_DEBUG, "removing rotated log (rotateCount == 0)");
		if (unlink(finalName)) {
		    fprintf(errorFile, "Failed to remove old log %s: %s\n",
				finalName, strerror(errno));
		    hasErrors = 1;
		}
	    }

	    if (!hasErrors && log->rotateCount && 
			(log->flags & LOG_FLAG_COMPRESS) &&
			!(log->flags & LOG_FLAG_DELAYCOMPRESS)) {
		char * command;

		command = alloca(strlen(finalName) + strlen(log->compress_prog) + 1 + strlen(log->compress_options) + 20);

		sprintf(command, "%s %s '%s'", log->compress_prog, log->compress_options, finalName);
		message(MESS_DEBUG, "compressing new log with: %s\n", command);
		if (!debug && system(command)) {
		    fprintf(errorFile, "failed to compress log %s\n", 
				finalName);
		    hasErrors = 1;
		}
	    }

	    if (!hasErrors && log->logAddress) {
		char * command;
		char * mailFilename;

		if (log->flags & LOG_FLAG_MAILFIRST)
		    mailFilename = firstRotated;
		else
		    mailFilename = disposeName;

		if (mailFilename) {
		    command = alloca(strlen(mailFilename) + 100 + 
				     strlen(log->uncompress_prog));

		    if ((log->flags & LOG_FLAG_COMPRESS) &&
			    !(log->flags & LOG_FLAG_DELAYCOMPRESS) &&
			    (log->flags & LOG_FLAG_MAILFIRST))
			sprintf(command, "%s < %s | %s '%s' %s", 
				    log->uncompress_prog, mailFilename, mailCommand,
				    log->files[logNum],
				    log->logAddress);
		    else
			sprintf(command, "%s '%s' %s < %s", mailCommand, 
				    mailFilename, log->logAddress, mailFilename);

		    message(MESS_DEBUG, "executing: \"%s\"\n", command);

		    if (!debug && system(command)) {
			sprintf(newName, "%s.%d", log->files[logNum], getpid());
			fprintf(errorFile, "Failed to mail %s to %s!\n",
				mailFilename, log->logAddress);

			hasErrors = 1;
		    } 
		}
	    }

	    if (!hasErrors && disposeName) {
		message(MESS_DEBUG, "removing old log %s\n", disposeName);

		if (!debug && unlink(disposeName)) {
		    fprintf(errorFile, "Failed to remove old log %s: %s\n",
				disposeName, strerror(errno));
		    hasErrors = 1;
		}
	    }

	}
    } else if (!doRotate) {
	message(MESS_DEBUG, "log does not need rotating\n");
    }

    return hasErrors;
}

int rotateLogSet(logInfo * log, logState ** statesPtr, int * numStatesPtr, 
		 int force) {
    int i;
    int hasErrors = 0;

    if (force)
	log->criterium = ROT_FORCE;

    message(MESS_DEBUG, "rotating pattern: %s ", log->pattern);
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
	message(MESS_DEBUG, "olddir is %s ", log->oldDir);

    if (log->flags & LOG_FLAG_IFEMPTY) 
	message(MESS_DEBUG, "empty log files are rotated ");
    else
	message(MESS_DEBUG, "empty log files are not rotated ");

    if (log->logAddress) {
	message(MESS_DEBUG, "old logs mailed to %s\n", log->logAddress);
    } else {
	message(MESS_DEBUG, "old logs are removed\n");
    }

    if (log->pre && (log->flags & LOG_FLAG_SHAREDSCRIPTS)) {
	message(MESS_DEBUG, "running shared prerotate script\n");
	if (runScript(log->pattern, log->pre)) {
	    fprintf(stderr, "error running shared prerotate script for %s-- 
			leaving old logs in place\n", log->pattern);
	    hasErrors = 1;
	}
    }

    for (i = 0; i < log->numFiles; i++)
	hasErrors |= rotateSingleLog(log, i, statesPtr, numStatesPtr, 
					stderr);

    if (log->post && (log->flags & LOG_FLAG_SHAREDSCRIPTS)) {
	message(MESS_DEBUG, "running shared postrotate script\n");
	if (runScript(log->pattern, log->post)) {
	    fprintf(stderr, 
	       "error running shared postrotate script for %s\n", log->pattern);
	    hasErrors = 1;
	}
    }

    return hasErrors;
}

static int writeState(char * stateFilename, logState * states, 
		      int numStates) {
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

    for (i = 0; i < numStates; i++) {
	fputc('"', f);
	for (chptr = states[i].fn; *chptr; chptr++) {
	    switch (*chptr) {
	      case '\\':
	      case '\'':
	      case '"':
	          fputc('\\', f);
	    }

	    fputc(*chptr, f);
	}

	fputc('"', f);
	fprintf(f, " %d-%d-%d\n", 
		states[i].lastRotated.tm_year + 1900,
		states[i].lastRotated.tm_mon + 1,
		states[i].lastRotated.tm_mday);
    }

    fclose(f);

    return 0;
}

static int readState(char * stateFilename, logState ** statesPtr, 
		     int * numStatesPtr) {
    FILE * f;
    char buf[1024];
    const char ** argv;
    int argc;
    int year, month, day;
    int i;
    int line = 0;
    logState * st;
    time_t lr_time;

    f = fopen(stateFilename, "r");

    if (!f && errno == ENOENT) {
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
    } else if (!f) {
	message(MESS_ERROR, "error creating state file %s: %s\n", 
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

	st = findState(argv[0], statesPtr, numStatesPtr);

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
			  /* pre, post */ NULL, NULL,
			  /* logAddress */ NULL, 
			  /* extension */ NULL, 
			  /* compression */ COMPRESS_COMMAND,
			  UNCOMPRESS_COMMAND, COMPRESS_OPTIONS, COMPRESS_EXT,
			  /* flags */ LOG_FLAG_IFEMPTY,
			  /* createMode */ NO_MODE, NO_UID, NO_GID };
    int numLogs = 0, numStates = 0;
    int force = 0;
    logInfo * logs = NULL;
    logState * states = NULL;
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
		"Command to use to mail logs", "command" },
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

    files = poptGetArgs(optCon);
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

    if (readState(stateFile, &states, &numStates)) {
	exit(1);
    }

    message(MESS_DEBUG, "Handling %d logs\n", numLogs);

    for (i = 0; i < numLogs; i++) {
	rc |= rotateLogSet(logs + i, &states, &numStates, force);
    }

    if (!debug && writeState(stateFile, states, numStates)) {
	exit(1);
    }

    return (rc != 0);
}
