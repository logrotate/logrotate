/*
 * OS-specific definitions
 */

#define ROOT_UID 0

#ifdef __hpux
#define DEFAULT_MAIL_COMMAND "/usr/bin/mailx"
#define COMPRESS_COMMAND "/usr/contrib/bin/gzip"
#define UNCOMPRESS_COMMAND "/usr/contrib/bin/gunzip"
#define OLDSTATEFILE "/var/run/logrotate.status"
#define STATEDIR "/var/run/logrotate/"
#define STATEFILE STATEDIR "logrotate.status"
#endif

#ifdef SunOS
#define DEFAULT_MAIL_COMMAND "/usr/bin/mailx"
#define OLDSTATEFILE "/var/log/logrotate.status"
#define STATEDIR "/var/log/logrotate/"
#define STATEFILE STATEDIR "logrotate.status"
#endif

#ifdef __NetBSD__
#define DEFAULT_MAIL_COMMAND "/usr/bin/mail -s"
#define COMPRESS_COMMAND "/usr/bin/gzip"
#define UNCOMPRESS_COMMAND "/usr/bin/gunzip"
#define OLDSTATEFILE "/var/log/logrotate.status"
#define STATEDIR "/var/log/logrotate/"
#define STATEFILE STATEDIR "logrotate.status"
#endif

#if defined(__APPLE__) && defined(__MACH__)
#define COMPRESS_COMMAND "/usr/bin/gzip"
#define UNCOMPRESS_COMMAND "/usr/bin/gunzip"
#endif

/*
 * Default settings for Linux - leave these last.
 */
#ifndef DEFAULT_MAIL_COMMAND
#define DEFAULT_MAIL_COMMAND "/bin/mail"
#endif

#ifndef COMPRESS_COMMAND
#define COMPRESS_COMMAND "/bin/gzip"
#endif

#ifndef COMPRESS_EXT
#define COMPRESS_EXT ".gz"
#endif

#ifndef UNCOMPRESS_COMMAND
#define UNCOMPRESS_COMMAND "/bin/gunzip"
#endif

#ifndef STATEFILE
#define OLDSTATEFILE "/var/lib/logrotate.status"
#define STATEDIR "/var/lib/logrotate/"
#define STATEFILE STATEDIR "logrotate.status"
#endif
