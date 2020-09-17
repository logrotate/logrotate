#!/bin/sh

. ./test-common.sh

# sanity rotation check using dateext and dateformat
cleanup 14

# ------------------------------- Test 14 ------------------------------------
preptest test.log 14 1 0

$RLR test-config.14 --force

DATESTRING=$(/bin/date +%Y-%m-%d)

checkoutput <<EOF
test.log 0
test.log.$DATESTRING 0 zero
EOF

rm -rf testdir
