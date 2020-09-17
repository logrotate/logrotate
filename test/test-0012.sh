#!/bin/sh

. ./test-common.sh

# check rotation into a directory given as a relative pathname
cleanup 12

# ------------------------------- Test 12 ------------------------------------
preptest test.log 12 1 0
rm -rf testdir
mkdir testdir
$RLR test-config.12 --force

checkoutput <<EOF
test.log 0
testdir/test.log.1 0 zero
EOF

rm -rf testdir
