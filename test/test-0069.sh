#!/bin/sh

. ./test-common.sh

cleanup 69

# ------------------------------- Test 69 ------------------------------------
# Test olddir with wildcard in the pattern
preptest test.log 69 1 0
rm -rf testdir adir bdir
mkdir adir
mkdir bdir
cp test.log adir
cp test.log bdir
$RLR test-config.69 --force

checkoutput <<EOF
adir/test.log 0
testdir/test.log.1 0 zero
EOF

rm -rf testdir adir
rm -rf testdir bdir
