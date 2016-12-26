AC_INIT([logrotate],[m4_esyscmd([build-aux/git-version-gen .tarball-version])],
[],[],[https://github.com/logrotate/logrotate])

dnl foreign: Do not require AUTHORS, ChangeLog, NEWS, and README to exist
dnl serial-tests: Do not hide standard output of our sequential test-suite
dnl dist-xz: Produce tar.xz version of the release archive
AM_INIT_AUTOMAKE([foreign serial-tests dist-xz])

# --enable-silent-rules
m4_ifdef([AM_SILENT_RULES],
  [AM_SILENT_RULES([yes])],
  [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

AC_USE_SYSTEM_EXTENSIONS

AC_PROG_CC
AC_PROG_CC_C99
AC_STRUCT_ST_BLKSIZE
AC_STRUCT_ST_BLOCKS

AC_CANONICAL_HOST

dnl Use 64-bit file offsets on 32-bit systems (defines C macros if necessary)
AC_SYS_LARGEFILE

AC_CHECK_LIB([popt],[poptParseArgvString],,
  AC_MSG_ERROR([libpopt required but not found]))

AC_CHECK_FUNCS([asprintf fork madvise qsort strndup strptime vfork vsyslog])

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

STATEFILE_val=''
AC_ARG_WITH([state-file-path],
  AC_HELP_STRING([--with-state-file-path=PATH],
                 [path to state file (/var/lib/logrotate.status by default)]),
  [
    case "$withval" in
      yes|no)
        AC_MSG_ERROR([--with-state-file-path=PATH requires a path to be specified])
        ;;
      *)
	STATEFILE_val="$withval"
        ;;
    esac
  ])
AS_IF([test x$STATEFILE_val != x], [
  state_file_path="$withval"
], [
  AS_CASE([${host_os}],
    [*hpux*],
      [state_file_path="/var/run/logrotate.status"],
    [state_file_path="/var/lib/logrotate.status"]
 )
])
AC_DEFINE_UNQUOTED([STATEFILE], ["$state_file_path"], [State file path.])
AC_SUBST([STATE_FILE_PATH], [$state_file_path])


default_mail_comm=''
AC_ARG_WITH([default-mail-command],
  AC_HELP_STRING([--with-default-mail-command=COMMAND],
                 [default mail command (e.g. /bin/mail -s)]),
  [
    case "$withval" in
      yes|no)
        AC_MSG_ERROR([--with-default-mail-command=COMMAND requires a command to be specified])
        ;;
      *)
        default_mail_comm="$withval"
        ;;
    esac
  ])
AS_IF([test "x$default_mail_comm" = x], [
  AS_CASE([${host_os}],
    [*hpux*],
      [default_mail_comm=/usr/bin/mailx],
    [*solaris*],
      [default_mail_comm=/usr/bin/mailx],
    [*netbsd*],
      [default_mail_comm=/usr/bin/mail -s],
    [default_mail_comm=/bin/mail]
  )
])
AC_DEFINE_UNQUOTED([DEFAULT_MAIL_COMMAND], "$default_mail_comm", [default mail command])
AC_SUBST(DEFAULT_MAIL_COMMAND)


default_compress_comm=''
AC_ARG_WITH([compress-command],
  AC_HELP_STRING([--with-compress-command=COMMAND],
                 [compress command]),
  [
    case "$withval" in
      yes|no)
        AC_MSG_ERROR([--with-compress-command=COMMAND requires a command to be specified])
        ;;
      *)
        default_compress_comm="$withval"
        ;;
    esac
  ])
AS_IF([test "x$default_compress_comm" = x], [
  AS_CASE([${host_os}],
    [*hpux*],
      [default_compress_comm=/usr/contrib/bin/gzip],
    [*darwin*],
      [default_compress_comm=/usr/bin/gzip],
    [*netbsd*],
      [default_compress_comm=/usr/bin/gzip],
    [default_compress_comm=/bin/gzip]
  )
])
AC_DEFINE_UNQUOTED([COMPRESS_COMMAND], "$default_compress_comm", [default compress command])
AC_SUBST(COMPRESS_COMMAND)


default_uncompress_comm=''
AC_ARG_WITH([uncompress-command],
  AC_HELP_STRING([--with-uncompress-command=COMMAND],
                 [uncompress command, default path: see configure output, often /usr/bin/gzip]),
  [
    case "$withval" in
      yes|no)
        AC_MSG_ERROR([--with-uncompress-command=COMMAND requires a command to be specified])
        ;;
      *)
        default_uncompress_comm="$withval"
        ;;
    esac
  ])
AS_IF([test "x$default_uncompress_comm" = x], [
  AS_CASE([${host_os}],
    [*hpux*],
      [default_uncompress_comm=/usr/contrib/bin/gunzip],
    [*darwin*],
      [default_uncompress_comm=/usr/bin/gunzip],
    [*netbsd*],
      [default_uncompress_comm=/usr/bin/gunzip],
    [default_uncompress_comm=/bin/gunzip]
  )
])
AC_DEFINE_UNQUOTED([UNCOMPRESS_COMMAND], "$default_uncompress_comm", [default uncompress command])
AC_SUBST(UNCOMPRESS_COMMAND)

COMPRESS_EXT='.gz'
AC_ARG_WITH([compress-extension],
  AC_HELP_STRING([--with-compress-extension=EXTENSION],
                 [compress extension (default: .gz)]),
  [
    case "$withval" in
      .*)
        COMPRESS_EXT="$withval"
        ;;
      *)
        AC_MSG_ERROR([--with-compress-extension=EXTENSION must start with .])
        ;;
    esac
  ])
AC_DEFINE_UNQUOTED([COMPRESS_EXT], "$COMPRESS_EXT", [default compress extension])
AC_SUBST(COMPRESS_EXT)


AC_DEFINE_UNQUOTED([ROOT_UID], [0], [Root user-id.])

AC_CONFIG_HEADERS([config.h])

AC_CONFIG_FILES([Makefile test/Makefile logrotate.8 logrotate.spec])
AC_OUTPUT

AC_MSG_RESULT([
  ${PACKAGE_NAME} ${VERSION}

  default mail command:   ${default_mail_comm}
  compress command:       ${default_compress_comm}
  uncompress command:     ${default_uncompress_comm}
  compress extension:     ${COMPRESS_EXT}
  statefile path:         ${STATE_FILE_PATH}
])
