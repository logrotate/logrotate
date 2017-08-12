#!/bin/bash

. ./test-common.sh

cleanup 18

# ------------------------------- Test 18 ------------------------------------
needle="$(uuidgen -r)"

preptest "$needle.log" 18 1
$RLR test-config.18 -l syslog --force

checkoutput <<EOF
$needle.log 0
$needle.log.1.Z 1 zero
EOF

rm -f "$needle.log" "$needle.log.1.Z"

(echo "gzip -f -9") | diff -u - compress-args
grep -E -q "^LOGROTATE_COMPRESSED_FILENAME=.+/$needle.log.1$" compress-env
if [ $? != 0 ]; then
      echo "LOGROTATE_COMPRESSED_FILENAME $needle environment variable not found."
      cat compress-env
      exit 3
fi

SYSLOG_TESTS=0
logger "logrotate test suite $needle" 2>/dev/null
if [ $? = 0 ]; then
	journalctl -b -n 50 _COMM='logger' 2>/dev/null | grep "$needle" 2>/dev/null >/dev/null
	if [ $? = 0 ]; then
		SYSLOG_TESTS=1
	fi
fi
if [ $SYSLOG_TESTS = 1 ]; then
	journalctl -b -n 50 _COMM='logrotate' 2>/dev/null|grep "$needle" 2>/dev/null >/dev/null
	if [ $? != 0 ]; then
		echo "syslog needle not found"
		exit 1
	fi
fi
