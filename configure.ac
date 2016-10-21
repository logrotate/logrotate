AC_INIT([logrotate],[3.10.0])

dnl foreign: Do not require AUTHORS, ChangeLog, NEWS, and README to exist
dnl serial-tests: Do not hide standard output of our sequential test-suite
AM_INIT_AUTOMAKE([foreign serial-tests])

AC_DEFINE(_GNU_SOURCE)

AM_EXTRA_RECURSIVE_TARGETS([test])

AC_CANONICAL_HOST
AC_PROG_CC
AC_PROG_CC_STDC
AC_STRUCT_ST_BLKSIZE
AC_STRUCT_ST_BLOCKS

dnl Use 64-bit file offsets on 32-bit systems (defines C macros if necessary)
AC_SYS_LARGEFILE

AC_CHECK_LIB([popt],[poptParseArgvString],,
  AC_MSG_ERROR([libpopt required but not found]))

dnl Needed for out-of-source builds
mkdir -p test

AC_ARG_WITH([selinux],
  [AS_HELP_STRING([--with-selinux],
    [support handling SELinux contexts (yes,no,check) @<:@default=check@:>@])],
  [],
  [with_selinux=check])
AS_CASE(["$with_selinux"],
  [yes], [AC_CHECK_LIB([selinux],[getfscreatecon_raw])],
  [no], [],
  [AC_CHECK_LIB([selinux],[getfscreatecon_raw])])
AS_IF([test "$ac_cv_lib_selinux_getfscreatecon_raw" = yes],
  echo "1" > ./test/test.SELINUX;, echo "0" > ./test/test.SELINUX;)

AC_ARG_WITH([acl],
  [AS_HELP_STRING([--with-acl],
    [support handling ACL (yes,no,check) @<:@default=check@:>@])],
  [],
  [with_acl=check])
AS_CASE(["$with_acl"],
  [yes], [AC_CHECK_LIB([acl],[acl_get_file])],
  [no], [],
  [AC_CHECK_LIB([acl],[acl_get_file])])
AS_IF([test "$ac_cv_lib_acl_acl_get_file" = yes],
  echo "1" > ./test/test.ACL;, echo "0" > ./test/test.ACL;)

compress_cmd="/bin/gzip"
compress_extension=".gz"
email_client="/bin/mail"
status_file="logrotate.status"
status_dir="/var/lib/"
uncompress_cmd="/bin/gunzip"

case "$host_os" in
*hpux*)
	email_client = "/usr/bin/mailx"
	compress_cmd = "/usr/contrib/bin/gzip"
	uncompress_cmd = "/usr/contrib/bin/gunzip"
	status_dir="/var/run/logrotate"
	;;
*solaris*)
	email_client = "/usr/bin/mailx"
	status_dir="/var/log/logrotate"
	;;
*macosx*)
	compress_cmd = "/usr/bin/gzip"
	uncompress_cmd = "/usr/bin/gunzip"
	;;
*netbsd*)
	status_dir="/var/log/logrotate"
	email_client = "/usr/bin/mail -s"
	compress_cmd = "/usr/bin/gzip"
	uncompress_cmd = "/usr/bin/gunzip"
	;;
esac

AC_ARG_WITH(
    [statefile-path],
    [AS_HELP_STRING(
        [--with-statefile-path=PATH],
        [set the path to state file]
    )],
    ,
    [with_statefile_path=$status_dir$status_file]
)

AC_SUBST([COMPRESS_COMMAND], ["${compress_cmd}"])
AC_SUBST([COMPRESS_EXT], ["${compress_extension}"])
AC_SUBST([STATE_FILE_PATH], ["${with_statefile_path}"])
AC_SUBST([DEFAULT_MAIL_COMMAND], ["${email_client}"])
AC_SUBST([UNCOMPRESS_COMMAND], ["${uncompress_cmd}"])
AC_SUBST([ROOT_UID], [0])

AC_CHECK_FUNCS([asprintf fork madvise qsort strndup strptime vfork vsyslog])

AC_CONFIG_FILES([Makefile test/Makefile logrotate.spec logrotate.8 logrotate.c config.c])
AC_OUTPUT
