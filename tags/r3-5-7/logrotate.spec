Summary: Rotates, compresses, removes and mails system log files.
Name: logrotate
Version: 3.5.7
Release: 1
Copyright: GPL
Group: System Environment/Base
Source: ftp://ftp.redhat.com/pub/redhat/code/logrotate/logrotate-%{PACKAGE_VERSION}.tar.gz
BuildRoot: /var/tmp/logrotate.root

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

%build
make RPM_OPT_FLAGS="$RPM_OPT_FLAGS"

%install
make PREFIX=$RPM_BUILD_ROOT MANDIR=%{_mandir} install
mkdir -p $RPM_BUILD_ROOT/etc/logrotate.d
mkdir -p $RPM_BUILD_ROOT/etc/cron.daily

install -m 644 examples/logrotate-default $RPM_BUILD_ROOT/etc/logrotate.conf
install -m 755 examples/logrotate.cron $RPM_BUILD_ROOT/etc/cron.daily/logrotate

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc CHANGES
%attr(0755, root, root) /usr/sbin/logrotate
%attr(0644, root, root) %{_mandir}/man8/logrotate.8*
%attr(0755, root, root) /etc/cron.daily/logrotate
%attr(0644, root, root) %config /etc/logrotate.conf
%attr(0755, root, root) %dir /etc/logrotate.d

%changelog
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
