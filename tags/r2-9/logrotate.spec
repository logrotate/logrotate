Summary: Rotates, compresses, and mails system logs
Name: logrotate
Version: 2.9
Release: 1
Copyright: GPL
Group: Utilities/System
Source: ftp://ftp.redhat.com/pub/redhat/code/logrotate/logrotate-%{PACKAGE_VERSION}.tar.gz
BuildRoot: /var/tmp/logrotate.root

%description
Logrotate is designed to ease administration of systems that generate
large numbers of log files. It allows automatic rotation, compression,
removal, and mailing of log files. Each log file may be handled daily,
weekly, monthly, or when it grows too large.

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
%attr(0644, root, root) /usr/man/man8/logrotate.8
%attr(0755, root, root) /etc/cron.daily/logrotate
%attr(0644, root, root) %config /etc/logrotate.conf
%attr(0755, root, root) %dir /etc/logrotate.d
