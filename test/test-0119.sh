#!/bin/sh

. ./test-common.sh

cleanup 119

# ------------------------------- Test 119 -----------------------------------
# An option value consisting only of whitespace, like the carriage return of
# a CRLF line ending, must be rejected like a missing value.  The config file
# contains a literal '\r' after "dateformat" (expanded from &CR&).

preptest test.log 119 1 0
createlog 0 anothertest.log
$RLR --force test-config.119 2>error.log && exit 23

grep "argument expected after dateformat" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi
if grep "failed to apply date format" error.log >/dev/null; then
	echo "The empty date format was not rejected while parsing."
	exit 3
fi

rm error.log

checkoutput <<EOF
test.log 0 zero
anothertest.log 0
anothertest.log.1 0 zero
EOF
