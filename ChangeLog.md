# logrotate change log

All notable changes to this project will be documented in this file.

## [UNRELEASED]

[UNRELEASED]: https://github.com/logrotate/logrotate/compare/3.18.1...master

## [3.18.1] - 2021-05-21
  - fix memory leaks on error-handling paths (#383, #387)
  - make `renamecopy` and `copytruncate` override each other (#386)
  - improve error handling in the parser of configuration files (#383)
  - improve user experience for non-privileged users of logrotate (#372)

[3.18.1]: https://github.com/logrotate/logrotate/compare/3.18.0...3.18.1

## [3.18.0] - 2021-01-08
  - allow UIDs and GIDs to be specified numerically (#217)
  - add support for Zstandard compressed files (#355)
  - make `delaycompress` not to fail with `rotate 0` (#341)

[3.18.0]: https://github.com/logrotate/logrotate/compare/3.17.0...3.18.0

## [3.17.0] - 2020-07-10
  - lock state file to prevent parallel execution of logrotate (#295, #297)
  - add `.bak` extension to default taboo list (#332)
  - allow to pass a home-relative path to `include` (#322, #326)
  - `switch_user_permanently`: skip switchback check if switched to root (#319)
  - logrotate.service: enable `ProtectClock` to restrict setting of clock (#306)
  - delete old logs hit by `maxage` regardless of `dateext` (#301)

[3.17.0]: https://github.com/logrotate/logrotate/compare/3.16.0...3.17.0

## [3.16.0] - 2020-02-28
  - fix compilation with gcc-10, which defaults to `-fno-common` (#288)
  - reduce number of `open()` operations on state file (#282)
  - validate user/group given to the `su` directive early (#276)
  - add hardening options to systemd unit file in examples (#274, #291)
  - fix crash of logrotate on invalid configuration with include (#271)
  - fix heap buffer overflow on too long logfile extension (#270)

[3.16.0]: https://github.com/logrotate/logrotate/compare/3.15.1...3.16.0

## [3.15.1] - 2019-08-30
  - use correct `create` mode in examples/btmp (#257)
  - fix several bugs found by fuzzing (#254)
  - do not abort globbing on a broken symlink (#251)
  - rearrange `logrotate.8` man page to improve readability (#248)
  - encourage admins to use the `su` directive in `logrotate.8` man page (#236)

[3.15.1]: https://github.com/logrotate/logrotate/compare/3.15.0...3.15.1

## [3.15.0] - 2018-12-04
  - timer unit: change trigger fuzz from 12h to 1h (#230)
  - service unit: only run if `/var/log` is mounted (#230)
  - preserve fractional part of timestamps when compressing (#226)
  - re-indent source code using spaces only (#188)
  - minage: avoid rounding issue while comparing the amount of seconds (#36)
  - never remove old log files if `rotate -1` is specified (#202)
  - return non-zero exit status if a config file contains an error (#199)
  - make `copytruncate` work with `rotate 0` (#191)
  - warn user if both size and the time interval options are used (#192)
  - pass rotated log file name as the 2nd argument of the `postrotate` script
    when sharedscript is not enabled (#193)
  - rename `logrotate-default` to `logrotate.conf` (#187)

[3.15.0]: https://github.com/logrotate/logrotate/compare/3.14.0...3.15.0

## [3.14.0] - 2018-03-09

  - make `configure` show support status for SELinux and ACL at the end (#179)
  - make logrotate build again on FreeBSD (#178)
  - move `wtmp` and `btmp` definitions from `logrotate.conf` to
    separate configuration files in `logrotate.d` (#168)
  - print a warning about logrotate doing nothing when `-d` is used (#165)
  - do not reject executable config files (#166)
  - add hardening options to `logrotate.service` in examples (#143)
  - fix spurious compressor failure when using `su` and `compress` (#169)
  - keep logrotate version in .tarball-version in release tarballs (#156)
  - introduce the `hourago` configuration directive (#159)
  - ignore empty patterns in `tabooext` to avoid exclusion of everything (#160)
  - properly report skipped test cases instead of pretending success

[3.14.0]: https://github.com/logrotate/logrotate/compare/3.13.0...3.14.0

## [3.13.0] - 2017-10-13

  - make distribution tarballs report logrotate version properly
    [RHBZ#1500264](https://bugzilla.redhat.com/1500264)
  - make `(un)compress work` even if stdin and/or stdout are closed (#154)
  - remove `-s` from `DEFAULT_MAIL_COMMAND` and improve its documentation (#152)
  - uncompress logs before mailing them even if `delaycompress` is enabled (#151)
  - handle unlink of a non-existing log file as a warning only (#144)
  - include compile-time options in the output of `logrotate --version` (#145)
  - make `logrotate --version` print to stdout instead of stderr (#145)
  - flush write buffers before syncing state file (#148)
  - specify (un)compress utility explicitly in tests (#137)
  - enable running tests in parallel (#132)
  - explicitly map root UID/GID to 0 on Cygwin (#133)
  - add `.dpkg-bak` and `.dpkg-del` to default tabooext list (#134)

[3.13.0]: https://github.com/logrotate/logrotate/compare/3.12.3...3.13.0

## [3.12.3] - 2017-06-30

  - `copy` and `copytruncate` directives now work together again
  - `unlink()` is no longer preceded by `open()` unless shred is enabled (#124)
  - `compress` and `uncompress` now take commands from `$PATH`, too (#122)

[3.12.3]: https://github.com/logrotate/logrotate/compare/3.12.2...3.12.3

## [3.12.2] - 2017-05-02

  - build fixes related to `-Werror` (#119) and `-Werror=format=` (#108)
  - `configure --enable-werror` now controls use of the -Werror flag (#123)

[3.12.2]: https://github.com/logrotate/logrotate/compare/3.12.1...3.12.2

## [3.12.1] - 2017-04-21

  - Included forgotten `build-aux` directory in release tarballs.

[3.12.1]: https://github.com/logrotate/logrotate/compare/3.12.0...3.12.1

## [3.12.0] - 2017-04-21

  - Fixed accident removal of rotated files with dateext. (#118)
  - Line comments inside globs in config files are now skipped. (#109)
  - logrotate now recovers from a corrupted state file. (#45)
  - `Makefile.legacy` has been removed. (#103)
  - `config.h` is now generated by autotools. (#102 and #103)
  - `createolddir` now creates old directory as unprivileged user. (#114)
  - `weekly` rotations are now predictable and configurable. (#93)
  - Errors in config files are no longer treated as fatal errors. (#81)
  - `configure --with-default-mail-command` specifies default mail command. (#100)
  - Fixed heap buffer overflow when parsing crafted config file. (#33)

[3.12.0]: https://github.com/logrotate/logrotate/compare/3.11.0...3.12.0

## [3.11.0] - 2016-12-02

  - Add 'taboopat' configuration directive to exclude configuration files
    based on globing patterns.
  - Allow to change default state path at build time (via the
    --with-state-file-path option of the configure script).
  - Automatically determine resulting file suffix based on the compression
    program in use.
  - Preserve SELinux context with 'compress' and 'sharedscripts' properly.
  - Rename already existing output files to avoid collisions.
  - Import systemd service and timer for logrotate from openSUSE.
  - Introduce the 'addextension' configuration directive.
  - Create 'CONTRIBUTING.md' with instructions for logrotate contributors.
  - Maintain ChangeLog.md instead of the legacy CHANGES file.
  - Make 'createolddir' configuration directive preserve sticky bit.
  - Add 'minage' configuration directive to specify minimum file age to rotate.
  - Avoid using local implementation of strndup() and asprintf() if these
    functions are available at build time.
  - Fix parsing of 'su' directive to accept usernames starting with numeric
    symbols.
  - Make sure that 64-bit file offsets are used on 32-bit systems.

[3.11.0]: https://github.com/logrotate/logrotate/compare/3.10.0...3.11.0

## [3.10.0] - 2016-08-03

  - Legacy Makefile renamed to Makefile.legacy, will be removed eventually.
  - Fix 'make dist' and 'make distcheck' to produce a usable release tarball.
  - Fix 'olddir' usage with wildcard in the middle of path in the pattern
    definition when the pattern did not match any log file.
  - Remove half-rotated files when rotation of particular log file is skipped
    because of an error during copy or compression.

[3.10.0]: https://github.com/logrotate/logrotate/compare/3.9.2...3.10.0

## [3.9.2] - 2016-01-20

  - Upstream moved to GitHub: <https://github.com/logrotate/logrotate>.
  - Add support for %M, %S and %V in "dateext" directive.
  - Fix bad filename in subject of email when "compress" and "maillast" is
    used.
  - Allow rotating files created before 1996.
  - Fix compilation errors on NetBSD caused by "array subscript has
    type 'char' in config.c"
  - Fix matching subdirectories on BSD systems for patterns like
    "*/log" in situation where logrotate tried to match "foo/log" even when
    "foo" has not been a directory.
  - Fix logging dates in debug messages.
  - Remove state file entries for logs which do not exist and have not been
    rotated for more than a year.
  - Fix poor performance with big state file.
  - Support logging to syslog by using '-l syslog'.
  - Allow running test-suite using dash.

[3.9.2]: https://github.com/logrotate/logrotate/compare/r3-9-1...3.9.2

## [3.9.1] - 2015-04-03

  - Fix off-by-one error which can lead to crash when copytruncate is used.

[3.9.1]: https://github.com/logrotate/logrotate/compare/r3-9-0...r3-9-1

## [3.9.0] - 2015-04-03

  - Fix crash when using long dateformat. [nmerdan]
  - Add support for %H dateformat. [czchen]
  - Fix regression introduced in 3.8.9 when when rotating multiple
    logs when one of them is missing.
  - In the debug mode, do not skip the code-path which handles the case when
    the last rotation does not exist. [Sergey Vidishev]
  - Show more precise description when "log does not need rotating".
  - Add new -l option to log verbose output to file. The file is overwritten
    on every logrotate execution.
  - Allow rotation of sparse files with copytruncate.

[3.9.0]: https://github.com/logrotate/logrotate/compare/r3-8-9...r3-9-0

## [3.8.9] - 2015-02-13

  - Add new directive "createolddir" and "nocreateolddir". These directives
    can be used to create the directory specified by olddir with particular
    "mode", "owner" and "group".
  - Continue with rotation even when first log from logset is removed
    during the rotation.
  - Fix crash on BSD systems introduced in 3.8.8 caused by different qsort_r
    function. Function qsort is now used instead.
  - Fix potential buffer overflow in usage of strncat function.
  - Fix compilation with musl-libc.
  - Add experimental 'renamecopy' directive to allow 'olddir' on different
    physical device. See the "man logrotate" for more information.

[3.8.9]: https://github.com/logrotate/logrotate/compare/r3-8-8...r3-8-9

## [3.8.8] - 2014-10-16

  - Add support for building using autotools/automake. Using "./autogen.sh",
    "./configure" and "make" is now preferred way how to build logrotate.
    Old Makefile remains available, but it is deprecated and will be removed
    in the future. Please report any problem related to new build system.
  - Add support for systems which do not support fork (use vfork instead)
    and madvise.
  - Fix bug when wrong log file has been removed in case of dateext and
    dateformat %d-%m-%Y.
  - Do not expect that the name of root account is 'root'.
  - Do not stop rotation with an error when olddir and log file
    are on different devices and copy or copytruncate is used.
  - Return an error code when parent directory of log does not exist,
    "su" directive is not used, logrotate is running as root and missingok
    is not specified. [vcizek]
  - Prepend error printed by compression program with the log name even when
    the compression program exits with zero exit code.

[3.8.8]: https://github.com/logrotate/logrotate/compare/r3-8-7...r3-8-8

## [3.8.7] - 2013-10-10

  - Fixed --force/-f option handling together with "size" directive
    (3.8.5 regression).
  - Use "logrotate_tmp_t" context for SELinux tests and if this context does
    not exist, skip SELinux related tests.

[3.8.7]: https://github.com/logrotate/logrotate/compare/r3-8-6...r3-8-7

## [3.8.6] - 2013-07-31

  - Fixed memory corruption caused by rotation directory which does not
    exist with "sharedscripts" together with "prerotate" script.

[3.8.6]: https://github.com/logrotate/logrotate/compare/r3-8-5...r3-8-6

## [3.8.5] - 2013-06-10

  - Improved rotation during daylight saving time and between timezone
    changes.
  - Fixed ACL setting problem caused by ext3 erroneously reporting ENOSYS
    instead of ENOSUP.
  - Do not continue with rotation if state file is corrupted.
  - Make logrotate.status creation atomic.
  - Allow "hourly" rotation. See manpage for more information.
  - Use "/bin/echo" in tests. Fixes tests execution in Dash.
  - Do no try to parse config files bigger than 16MB.
  - Improved manpage consistency and formatting.
  - Fix race condition between acl_set_fd() and fchmod().

[3.8.5]: https://github.com/logrotate/logrotate/compare/r3-8-4...r3-8-5

## [3.8.4] - 2013-04-30

  - Added --version command line option
  - Disable ACL tests if logrotate is not compiled WITH_ACL support or if
    ACLs are not supported by the system running tests
  - Disable SELinux tests if logrotate is not compiled WITH_SELINUX support
    or if SELinux is not supported by the system running tests
  - Fixed bug which prevented skipping particular log file config
    if the config contained errors.
  - Fixed skipping of configs containing firstaction/lastaction scripts
    with '}' character in case of error before these scripts.
  - Support also 'K' unit for *size directives.
  - Added preremove option to let admin to do something with the old logs
    before they are removed by logrotate.
  - Fixed possible loop in tabooext parsing.
  - Move code to set SELinux context before compressLogFile calls to create
    compressed log files with the proper context.
  - Call prerotate/postrotate script only for really rotated files in
    nosharedscripts mode (as stated in man page).

[3.8.4]: https://github.com/logrotate/logrotate/compare/r3-8-3...r3-8-4

## [3.8.3] - 2012-10-04

  - Fixed setting "size" bigger than 4GB on 32bit architectures
  - Do not overwrite mode set by "create" option when using ACL. "create"
    directive is now not mixed up with ACLs. If you use "create" in config
    file and log file has some ACLs set, ACLs are not kept and are
    overwritten by the mode set in "create" directive.
  - Mode argument in "create" directive can be omitted. Only owner and group
    is set in this case. Check man page for more info.

[3.8.3]: https://github.com/logrotate/logrotate/compare/r3.8.2...r3-8-3

## [3.8.2] - 2012-08-01

  - show error and ignore config if '{' is not present after log files
    declaration
  - support whitespaces in compressoptions directive
  - support for tilde expansion in config files
  - 'su' directive does not affect script execution - scripts
    are executed as a root if 'su' directive is present
  - fixed mail sending for 'mailfirst', 'dateext' and 'delaycompress'
    combination
  - do not use gzip/gunzip from /usr/local on Solaris
  - add O_NOFOLLOW when opening files as safeguard against symlink tricks.
    Symlinks rotation is now officially unsupported. It didn't work
    as expected in the past anyway.
  - do not run external programs with uid != euid
  - fixed potential bad-free when ACL is used
  - Do not include alloca.h on NetBSD, since alloca() is declared in
    stdlib.h there
  - 13 new tests added

[3.8.2]: https://github.com/logrotate/logrotate/compare/r3-8-1...r3.8.2

## [3.8.1] - 2011-08-31

  - fixed 1 memory leak in prerotateSingleLog
  - another fixes for Solaris
  - fixed HP-UX compilation and default config
  - do not redirect logrotate errors to /dev/null in cron script
  - fixed "size" directive parsing
  - handle situation when acl_get_fd is supported, but acl_set_fd is not
  - added "maxsize" directive (see man page)

[3.8.1]: https://github.com/logrotate/logrotate/compare/r3-8-0...r3-8-1

## [3.8.0] - 2011-06-21

  - added "dateyesterday" option (see man page)
  - fixed crash when config file had exactly 4096*N bytes
  - added WITH_ACL make option to link against -lacl and preserve ACLs
    during rotation
  - added "su" option to define user/group for rotation. Logrotate now
    skips directories which are world writable or writable by group
    which is not "root" unless "su" directive is used.
  - fixed CVE-2011-1098: race condition by creation of new files
  - fixed possible shell injection when using "shred" directive (CVE-2011-1154)
  - fixed escaping of file names within 'write state' action (CVE-2011-1155)
  - better 'size' directive description
  - fixed possible buffer-overflow when reading config files
  - NetBSD/FreeBSD compilation fixes
  - Solaris compilation fixes

[3.8.0]: https://github.com/logrotate/logrotate/compare/r3-7-9...r3-8-0

## [3.7.9] - 2010-06-28

  - fix building on Solaris (patch by András Szilárd)
  - don't copy config files on the stack -- mmap them instead
    (fixes segfaults with too large/invalid config files)
  - symlinked conf file man page as requested by Fedora guidelines
    (thanks to Ivana Hutarova Varekova)
  - cron script logrotate.cron redirects output to /dev/null
  - added rotating (copying) non-writable, readable files
    (patch by Henrique Martins)
  - fixed missingok problem with globs
    (taken from the Debian patches by Ted Percival
     <ted@midg3t.net>)
  - fixed bug when log files could be removed even there was
    some error in rotation process.
  - allow setting size greater than 4.2GB in configuration file
  - pass currently rotated file to postrotate/prerotate script
    in nosharedscripts mode
  - added new TabooExts: ".disabled", ".dpkg-old", ".dpkg-dist",
    ".dpkg-new", ".cfsaved", ".ucf-old", ".ucf-dist", ".ucf-new"
    (taken from the Debian patches by Paul Martin <pm@debian.org>)
  - Don't change utime atime/mtime when compressing files
    (taken from the Debian patches by Paul Martin <pm@debian.org>)
  - Better *rotate scripts parser. (taken from the Debian patches)
  - Allow 'include' directive in log file definitions

[3.7.9]: https://github.com/logrotate/logrotate/compare/r3-7-8...r3-7-9

## [3.7.8] - 2009-01-28

  - do not exit on status file errors
  - limit config file inclusion nesting
  - use hashes for status file handling (patch by Petr Tesarik
    <ptesarik@suse.cz> and Leonardo Chiquitto)
  - dateformat to allow unixtime (patch by Sami Kerola
    <kerolasa@iki.fi>)
  - manual page corrections (taken from the Debian patches by
    Paul Martin <pm@debian.org>)

[3.7.8]: https://github.com/logrotate/logrotate/compare/r3-7-7...r3-7-8

## [3.7.7] - 2008-05-19

  - dateformat
  - fix possible buffer overflows in strings handling
  - various minor bugfixes
  - change logInfo handling (patches by Leonardo Chiquitto)

[3.7.7]: https://github.com/logrotate/logrotate/compare/r3-7-6...r3-7-7

## [3.7.6] - 2008-05-14

  - patches from Leonardo Chiquitto that fix compile warnings
  - examples/logrotate-default: add btmp rotation, dateext
  - update man page
  - tabooext honor wildcards
  - fix selinux support with dateext

[3.7.6]: https://github.com/logrotate/logrotate/compare/r3-7-5...r3-7-6

## [3.7.5] - 2007-03-01

  - import Fedora patches
  - add option to use shred for deleting files, patch by
    Peter Eckersley <pde@eff.org>
  - ignore .cfsaved files
  - bugfixes

[3.7.5]: https://github.com/logrotate/logrotate/compare/r3-7-1...r3-7-5

## [3.7.1] - 2004-10-20

  - Fix sending mails and running scripts after the
    system() -> execve() changes
  - Preserve file attributes when compressing files (original patch
    by Daniel Himler)

[3.7.1]: https://github.com/logrotate/logrotate/compare/r3-7...r3-7-1

## [3.7] - 2004-01-26

  - always use compressext for the extension for compressed
    files; before compresscmd and compressext had to agree
  - moved all compression to one code block
  - compression, scripts don't use system() anymore
  - compress and maillast didn't work together properly
  - delaycompress and mailfirst didn't work properly
  - don't use system() for mailing (or uncompressing) logs anymore
  - use "-s" for specifying the subjected of mailed logs

[3.7]: https://github.com/logrotate/logrotate/compare/r3-6...r3-7

## [3.6] - 2001-11-28

  - See .spec file for changes

[3.6]: https://github.com/logrotate/logrotate/compare/r3-5-4...r3-6

## [3.5.4] - 2001-01-05

  - `%defattr(-,root,root)` in specfile

[3.5.4]: https://github.com/logrotate/logrotate/compare/r3-5-3...r3-5-4

## [3.5.3] - 2001-01-03

  - patch /tmp file race condition problem, use mkstemp;
    Thanks go to Solar Designer <solar@openwall.com>

[3.5.3]: https://github.com/logrotate/logrotate/compare/r3-5-2...r3-5-3

## [3.5.2] - 2000-09-29

  - added .swp and .rpmnew to default taboo list

[3.5.2]: https://github.com/logrotate/logrotate/compare/r3-5-1...r3-5-2

## [3.5.1] - 2000-08-11

  - handle state dates in the future a bit more sanely

[3.5.1]: https://github.com/logrotate/logrotate/compare/r3-5...r3-5-1

## [3.5] - 2000-07-23

  - multiple file names/patterns may be given for a single entry
  - fixed mistake in when logs were uncompressed before mailing

[3.5]: https://github.com/logrotate/logrotate/compare/r3-4...r3-5

## [3.4] - 2000-07-13

  - added sharedscripts/nosharedscripts
  - added simple testbed
  - quote filenames in state file to allow proper rotation of files
    with spaces in the name -- this changes the version number of
    the state file!
  - ignore white space at end of line

[3.4]: https://github.com/logrotate/logrotate/compare/r3-3-2...r3-4

## [3.3.2] - 2000-06-19

  - don't rotate lastlog

[3.3.2]: https://github.com/logrotate/logrotate/compare/r3-3-1...r3-3-2

## [3.3.1] - 2000-02-03

  - support gzipped man pages

[3.3.1]: https://github.com/logrotate/logrotate/compare/r3-3...r3-3-1

## [3.3] - 1999-06-16

  - added "mailfirst" and "maillast" flags (based on Tim Wall's patch)
  - documented "extension" flag
  - "rotate 0" gives proper script and mail behavior

[3.3]: https://github.com/logrotate/logrotate/compare/r3-2...r3-3

## [3.2] - 1999-04-07

  - create wtmp with correct perms

[3.2]: https://github.com/logrotate/logrotate/compare/r3-1...r3-2

## [3.1] - 1999-04-01

  - fixed small alloca()
  - added missingok flag
  - use popt to display usage message
  - handle /some/file { } in config file

[3.1]: https://github.com/logrotate/logrotate/compare/r3-0...r3-1

## [3.0] - 1999-03-18

  - updates for glibc 2.1

[3.0]: https://github.com/logrotate/logrotate/compare/r2-9...r3-0

## [2.9] - 1999-03-05

  - fixed a bug parsing lines where { immediately follows the filename
  - allow log file patterns to be placed in double quotes, which
    allows spaces in names
  - complain about missing log files (John Van Essen)

[2.9]: https://github.com/logrotate/logrotate/compare/r2-8...r2-9

## [2.8] - 1999-01-13

  - changes for glibc 2.1 (Cristian Gafton)

[2.8]: https://github.com/logrotate/logrotate/compare/r2-7...r2-8

## [2.7] - 1998-12-29

  - updated man page to include --force (Simon Mudd)
  - invoke scripts via /bin/sh rather then relying on /tmp execute
    semantics (Philip Guenther)
  - added "extension" option for forcing a file extension after rotation
    (Rob Hagopian)

[2.7]: https://github.com/logrotate/logrotate/compare/r2-6...r2-7

## [2.6] - 1998-05-05

  - added nodelaycompress flag (from Jos Vos)
  - added copytruncate, nocopytruncate flag (from Jos Vos)
  - removed umask handling; explicitly use fchmod() insteadmoved umask
  - added --force option (Simon Mudd)
  - moved /bin/mail to MAIL_COMMAND define (Simon Mudd)
  - fixed segv caused by overly long filenames
  - switched from getopt_long to popt

[2.6]: https://github.com/logrotate/logrotate/compare/r2-5...r2-6

## [2.5] - 1997-09-01

  - set the umask of the process to 0, letting open() create processes
    with the proper permissions
  - added delaycompress flag (from Jos Vos)
  - fixed how old logs are finally removed when an olddir is specified
    (Jos Vos)
  - added nomail option
  - added mail, nomail documentation to man page
  - added the tabooext directive
  - fixed problem in globbing

[2.5]: https://github.com/logrotate/logrotate/compare/r2-4...r2-5

## [2.4] - 1997-08-11

  - glob log names in config file
  - added ,v to taboo list
  - fixed bug w/ create parsing
  - use an int rather then a mode_t when parsing create entries as
    sscanf requires it

[2.4]: https://github.com/logrotate/logrotate/compare/2-3...r2-4

## [2.3] - 1997-03-18

  - fill in all of last rotated structure (this probable isn't
    really necessary but it's a bit cleaner and will avoid future
    problems);
  - fixed .spec file

[2.3]: https://github.com/logrotate/logrotate/compare/2-2...2-3

## [2.2] - 1997-02-27

  - If a file is rotated and we have no state information for it,
    right out the current time.
  - Weekly rotation happens when the current weekday is less then
    the weekday of the last rotation or more then a week has
    elapsed between the last rotation and now
  - Monthly rotation happens when the current month is different
    from the last month or the current year is different from the
    last year
  - (these were contributed and suggested by Ronald Wahl)
  - added olddir/noolddir options
  - added ifempty/notifempty options
  - ignore nonnormal files when reading config files from a directory
  - (these were suggested and originally implemented by
    Henning Schmiedehausen)
  - updated the man page to reflect these changes
  - made "make install" accept PREFIX argument
  - added .spec file to tarball

[2.2]: https://github.com/logrotate/logrotate/compare/2-1...2-2

## [2.1] - 1997-01-13

  - Don't output state information for logs that have never been
    rotated (better then 1900-1-0)
  - Accept 1900-1-0 as time 0

[2.1]: https://github.com/logrotate/logrotate/compare/2-0-2...2-1

## [2.0.2] - 1996-12-10

  - I have no idea :-(

[2.0.2]: https://github.com/logrotate/logrotate/compare/2-0-1...2-0-2

## [2.0.1] - 1996-12-09

  - ignore files in included directories which end with ~, .rpmorig, or
    .rpmsave

[2.0.1]: https://github.com/logrotate/logrotate/commits/2-0-1

<!--
vim:et:sw=2:ts=2
-->
