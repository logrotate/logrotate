#!/bin/sh

. ./test-common.sh

cleanup 18

# ------------------------------- Test 18 ------------------------------------
preptest test.log 18 1
$RLR test-config.18 -l syslog --force

checkoutput <<EOF
test.log 0
test.log.1.Z 1 zero
EOF

(echo "gzip -f -9") | diff -u - compress-args
egrep -q '^LOGROTATE_COMPRESSED_FILENAME=.+/test.log.1$' compress-env
if [ $? != 0 ]; then
      echo "LOGROTATE_COMPRESSED_FILENAME environment variable not found."
      cat compress-env
      exit 3
fi

SYSLOG_TESTS=0
logger syslog_test 2>/dev/null
if [ $? = 0 ]; then
	journalctl -n 50 2>/dev/null | grep syslog_test 2>/dev/null >/dev/null
	if [ $? = 0 ]; then
		SYSLOG_TESTS=1
	fi
fi
if [ $SYSLOG_TESTS = 1 ]; then
	journalctl -n 50 2>/dev/null|grep $PWD/test.log.1 2>/dev/null >/dev/null
	if [ $? != 0 ]; then
		echo "syslog message not found"
		exit 1
	fi
fi
