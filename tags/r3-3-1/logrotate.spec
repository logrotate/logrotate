Summary: Rotates, compresses, removes and mails system log files.
Name: logrotate
Version: 3.3.1
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
make PREFIX=$RPM_BUILD_ROOT install
mkdir -p $RPM_BUILD_ROOT/etc/logrotate.d
mkdir -p $RPM_BUILD_ROOT/etc/cron.daily

install -m 644 examples/logrotate-default $RPM_BUILD_ROOT/etc/logrotate.conf
install -m 755 examples/logrotate.cron $RPM_BUILD_ROOT/etc/cron.daily/logrotate

%clean
rm -rf $RPM_BUILD_ROOT

%files
%attr(0755, root, root) /usr/sbin/logrotate
%attr(0644, root, root) /usr/man/man8/logrotate.8*
%attr(0755, root, root) /etc/cron.daily/logrotate
%attr(0644, root, root) %config /etc/logrotate.conf
%attr(0755, root, root) %dir /etc/logrotate.d

%changelog

* Thu Feb 03 2000 Erik Troan <ewt@redhat.com>
- gzipped manpages
