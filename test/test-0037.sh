#!/bin/sh

. ./test-common.sh

cleanup 37

# ------------------------------- Test 37 ------------------------------------
# skip config with firstaction script
preptest test.log 37 1 0

$RLR test-config.37 --force 2>error.log

grep "skipping" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error 'skipping' printed, but there should be one."
	exit 3
fi

checkoutput <<EOF
test.log 0
test.log.1 0 zero
scriptout 0 second
EOF
