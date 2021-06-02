#!/bin/sh

. ./test-common.sh

# check rotation into a directory given as a relative pathname
cleanup 12

# ------------------------------- Test 12 ------------------------------------
preptest test.log 12 1 0
rm -rf testdir

# fail due to missing olddir
$RLR test-config.12 --force && exit 23

mkdir testdir

$RLR test-config.12 --force || exit 23

checkoutput <<EOF
test.log 0
testdir/test.log.1 0 zero
EOF

rm -rf testdir
