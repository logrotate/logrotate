%if %{?WITH_SELINUX:0}%{!?WITH_SELINUX:1}
%define WITH_SELINUX 1
BuildRequires: libselinux-devel
%endif
Summary: Rotates, compresses, removes and mails system log files.
Name: logrotate
Version: 3.7.2
Release: 1
License: GPL
Group: System Environment/Base
Source: logrotate-%{PACKAGE_VERSION}.tar.gz
#Patch0: logrotate-3.7.1-share.patch
Patch1: logrotate-3.7.1-man.patch
Patch2: logrotate-3.7.1-conf.patch
Patch3: logrotate-3.7.1-noTMPDIR.patch
Patch4: logrotate-3.7.1-selinux.patch
Patch5: logrotate-3.7.1-dateext.patch
Patch6: logrotate-3.7.1-maxage.patch
Patch7: logrotate-3.7.1-scriptFailInfo.patch
BuildRoot: %{_tmppath}/%{name}-%{version}.root

%description
The logrotate utility is designed to simplify the administration of
log files on a system which generates a lot of log files.  Logrotate
allows for the automatic rotation compression, removal and mailing of
log files.  Logrotate can be set to handle a log file daily, weekly,
monthly or when the log file gets to a certain size.  Normally,
logrotate runs as a daily cron job.

Install the logrotate package if you need a utility to deal with the
log files on your system.

%prep
%setup
#%patch0 -p1 -b .share
%patch1 -p1 -b .orig
%patch2 -p1 -b .conf
%patch3 -p1 -b .noTMPDIR
%patch4 -p1 -b .selinux
%patch5 -p1 -b .dateext
%patch6 -p1 -b .maxage
%patch7 -p1 -b .scriptFailInfo

%build
make RPM_OPT_FLAGS="$RPM_OPT_FLAGS -g" \
%if %{WITH_SELINUX}
	WITH_SELINUX=yes
%endif

%install
rm -rf $RPM_BUILD_ROOT
make PREFIX=$RPM_BUILD_ROOT MANDIR=%{_mandir} install
mkdir -p $RPM_BUILD_ROOT/etc/logrotate.d
mkdir -p $RPM_BUILD_ROOT/etc/cron.daily
mkdir -p $RPM_BUILD_ROOT/var/lib

install -m 644 examples/logrotate-default $RPM_BUILD_ROOT/etc/logrotate.conf
install -m 755 examples/logrotate.cron $RPM_BUILD_ROOT/etc/cron.daily/logrotate
touch $RPM_BUILD_ROOT/var/lib/logrotate.status

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc CHANGES
%attr(0755, root, root) /usr/sbin/logrotate
%attr(0644, root, root) %{_mandir}/man8/logrotate.8*
%attr(0755, root, root) /etc/cron.daily/logrotate
%attr(0644, root, root) %config(noreplace) /etc/logrotate.conf
%attr(0755, root, root) %dir /etc/logrotate.d
%attr(0644, root, root) %verify(not size md5 mtime) %config(noreplace) /var/lib/logrotate.status

%changelog
* Mon Aug 01 2005 Peter Vrabec <pvrabec@redhat.com> 3.7.2-1
- new upstream release

* Tue Jul 26 2005 Peter Vrabec <pvrabec@redhat.com> 3.7.1-14
- fix some "error running script" messages

* Tue Jul 26 2005 Peter Vrabec <pvrabec@redhat.com> 3.7.1-13
- fix man page (#163458,#163366)

* Wed Jun 22 2005 Peter Vrabec <pvrabec@redhat.com> 3.7.1-12
- enhance logrotate with "dateext", "maxage"

* Thu Mar 31 2005 Dan Walsh <dwalsh@redhat.com> 3.7.1-10
- use security_getenforce() instead of selinux_getenforcemode

* Thu Mar 17 2005 Dan Walsh <dwalsh@redhat.com> 3.7.1-9
- Add selinux_getenforce() calls to work when not in enforcing mode

* Thu Mar 17 2005 Peter Vrabec <pvrabec@redhat.com> 3.7.1-8
- rebuild

* Tue Feb 22 2005 Peter Vrabec <pvrabec@redhat.com>
- do not use tmpfile to run script anymore (#149270)

* Fri Feb 18 2005 Peter Vrabec <pvrabec@redhat.com>
- remove logrotate-3.7.1-share.patch, it doesn't solve (#140353)

* Mon Dec 13 2004 Peter Vrabec <pvrabec@redhat.com> - 3.7.1-5
- Add section to logrotate.conf for "/var/log/btmp" (#117844)

* Mon Dec 13 2004 Peter Vrabec <pvrabec@redhat.com> - 3.7.1-4
- Typo and missing information in man page (#139346)

* Mon Dec 06 2004 Peter Vrabec <pvrabec@redhat.com> - 3.7.1-3
- compressed logfiles and logrotate (#140353)

* Tue Oct 19 2004 Miloslav Trmac <mitr@redhat.com> - 3.7.1-2
- Fix sending mails (#131583)
- Preserve file attributes when compressing files (#121523, original patch by
  Daniel Himler)

* Fri Jul 16 2004 Elliot Lee <sopwith@redhat.com> 3.7.1-1
- Fix #126490 typo

* Tue Jun 15 2004 Elliot Lee <sopwith@redhat.com>
- rebuilt

* Fri Feb 13 2004 Elliot Lee <sopwith@redhat.com>
- rebuilt

* Mon Jan 26 2004 Dan Walsh <dwalsh@redhat.com> 3.6.10-4
- fix is_selinux_enabled call

* Fri Sep 5 2003 Dan Walsh <dwalsh@redhat.com> 3.6.10-3
- Turn off selinux

* Fri Sep 5 2003 Dan Walsh <dwalsh@redhat.com> 3.6.10-2.sel
- Turn on selinux

* Wed Aug 06 2003 Erik Troan <ewt@redhat.com>
- always use compressext for the extension for compressed
  files; before compresscmd and compressext had to agree
- moved all compression to one code block
- compression, scripts don't use system() anymore
- compress and maillast didn't work together properly
- delaycompress and mailfirst didn't work properly
- don't use system() for mailing (or uncompressing) logs anymore
- use "-s" for speciying the subjected of mailed logs

* Thu Jul 24 2003 Elliot Lee <sopwith@redhat.com> 3.6.10-1
- Fix #100546, change selinux port.

* Wed Jul 18 2003 Dan Walsh <dwalsh@redhat.com> 3.6.9-2
- Port to SELinux 2.5

* Wed Jul 09 2003 Elliot Lee <sopwith@redhat.com> 3.6.9-1
- Fix #90229, #90274, #89458, #91408

* Mon Jan 20 2003 Elliot Lee <sopwith@redhat.com> 3.6.8-1
- Old patch from pm@debian.org

* Tue Jan 14 2003 Elliot Lee <sopwith@redhat.com> 3.6.7-1
- Fixes from bugzilla

* Fri Nov 15 2002 Elliot Lee <sopwith@redhat.com> 3.6.6-1
- Commit patch from Fidelis Assis <fidelis@embratel.net.br>

* Thu Jun 20 2002 Elliot Lee <sopwith@redhat.com> 3.6.5-1
- Commit fix for #65299

* Mon Apr 15 2002 Elliot Lee <sopwith@redhat.com> 3.6.4-1
- Commit fix for #62560

* Wed Mar 13 2002 Elliot Lee <sopwith@redhat.com> 3.6.3-1
- Apply various bugfix patches from the openwall people

* Tue Jan 29 2002 Elliot Lee <sopwith@redhat.com> 3.6.2-1
- Fix bug #55809 (include logrotate.status in %files)
- Fix bug #58328 (incorrect error detection when reading state file)
- Allow 'G' size specifier from bug #57242

* Mon Dec 10 2001 Preston Brown <pbrown@redhat.com>
- noreplace config file

* Wed Nov 28 2001 Preston Brown <pbrown@redhat.com> 3.6-1
- patch from Alexander Kourakos <awk@awks.org> to stop the shared
  postrotate/prerotate scripts from running if none of the log(s) need
  rotating.  All log files are now checked for rotation in one batch,
  rather than sequentially.
- more fixes from Paul Martin <pm@debian.org>

* Thu Nov  8 2001 Preston Brown <pbrown@redhat.com> 3.5.10-1
- fix from paul martin <pm@debian.org> for zero-length state files

* Tue Sep  4 2001 Preston Brown <pbrown@redhat.com>
- fix segfault when logfile is in current directory.

* Tue Aug 21 2001 Preston Brown <pbrown@redhat.com>
- fix URL for source location

* Thu Aug  2 2001 Preston Brown <pbrown@redhat.com>
- man page cleanups, check for negative rotation counts

* Mon Jul  2 2001 Preston Brown <pbrown@redhat.com>
- more minor manpage updates (#45625)

* Thu Jun 21 2001 Preston Brown <pbrown@redhat.com> 3.5.6-1
- enable LFS support (debian bug #100810)
- quote filenames for running compress commands or pre/postrotate cmds (#21348)
- deprecate "errors" directive (see bug #16544 for explanation)
- update man page
- configurable compression command by Colm Buckley <colm@tuatha.org>

* Fri Jun  1 2001 Preston Brown <pbrown@redhat.com> 3.5.5-1
- be less strict about whitespace near filenames.  Patch from Paul Martin <pm@debian.org>.

* Thu Jan  4 2001 Bill Nottingham <notting@redhat.com>
- %%defattr

* Wed Jan 03 2001 Preston Brown <pbrown@redhat.com>
- see CHANGES

* Tue Aug 15 2000 Erik Troan <ewt@redhat.com>
- see CHANGES

* Sun Jul 23 2000 Erik Troan <ewt@redhat.com>
- see CHANGES

* Tue Jul 11 2000 Erik Troan <ewt@redhat.com>
- support spaces in filenames
- added sharedscripts

* Sun Jun 18 2000 Matt Wilson <msw@redhat.com>
- use %%{_mandir} for man pages

* Thu Feb 24 2000 Erik Troan <ewt@redhat.com>
- don't rotate lastlog

* Thu Feb 03 2000 Erik Troan <ewt@redhat.com>
- gzipped manpages
