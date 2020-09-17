#!/bin/sh

. ./test-common.sh

cleanup 51

# ------------------------------- Test 51 ------------------------------------
# regression in 3.8.4, logrotate crashes with sharedscripts when 0 logs rotated
preptest test.log 51 1 0

# It's memory corruption and without something in state file, it won't crash
# reliably. It would be better to run valgrind here and check the errors, but
# I don't want the test-suite to depend on valgrind...
cat > state << EOF
logrotate state -- version 2
"/var/log/httpd/backend_error_log" 2013-6-16
"/var/log/tokyotyrant/*.log" 2011-5-30
"/var/log/mailman/digest" 2011-5-30
"/var/log/piranha/piranha-gui-access" 2011-5-30
"/var/log/boincerr.log" 2011-5-30
"/var/log/btmp" 2013-7-9
"/var/log/httpd/a_log" 2011-11-15
"/var/log/cups/*_log" 2012-7-19
"/var/log/rabbitmq/*.log" 2011-5-30
"/var/log/func/func.log" 2011-11-17
"/var/log/wtmp" 2013-7-9
"/var/log/glusterfs/*glusterd.vol.log" 2011-11-17
"/var/log/imapd.log" 2011-5-30
"/var/log/cobbler/cobbler.log" 2011-11-6
"/var/log/httpd/ssl_access_log" 2013-3-27
"/var/log/mrepo.log" 2011-5-30
EOF

$RLR test-config.51

if [ $? != 0 ]; then
	echo "logrotate ended with non-zero exit code (probably crashed)"
	exit 3
fi
