#!/bin/sh

. ./test-common.sh

cleanup 79

# ------------------------------- Test 79 ------------------------------------
preptest test.log 79 1
$RLR test-config.79 --force >verbose.log
if [ $? != 0 ]; then
	echo "Logrotate exited with a non-zero exit code, but it should not have"
	exit 3
fi

grep -e "^Final rotated log filename should be empty in prerotate: $" verbose.log >/dev/null
if [ $? != 0 ]; then
	echo "Expected prerotate message to not include final filename."
	exit 3
fi

grep -e "^Final rotated log filename is: .*test.log.1$" verbose.log >/dev/null
if [ $? != 0 ]; then
	echo "Expected postrotate message not printed."
	exit 3
fi
