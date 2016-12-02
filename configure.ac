AC_INIT([logrotate],[3.11.0])

dnl foreign: Do not require AUTHORS, ChangeLog, NEWS, and README to exist
dnl serial-tests: Do not hide standard output of our sequential test-suite
AM_INIT_AUTOMAKE([foreign serial-tests])

AC_DEFINE(_GNU_SOURCE)

AM_EXTRA_RECURSIVE_TARGETS([test])

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

AC_ARG_WITH([state-file-path],
  AC_HELP_STRING([--with-state-file-path=PATH],
                 [path to state file (/var/lib/logrotate.status by default)]),
  [
    case "$withval" in
      yes|no)
        AC_MSG_ERROR([--with-state-file-path=PATH requires a path to be specified])
        ;;
      *)
        STATE_FILE_PATH="$withval"
        ;;
    esac
  ],
  [
    STATE_FILE_PATH="/var/lib/logrotate.status"
  ])
AC_DEFINE_UNQUOTED([STATEFILE], "$STATE_FILE_PATH")
AC_SUBST(STATE_FILE_PATH)

AC_CHECK_FUNCS([asprintf fork madvise qsort strndup strptime vfork vsyslog])

AC_CONFIG_FILES([Makefile test/Makefile logrotate.8 logrotate.spec])
AC_OUTPUT
