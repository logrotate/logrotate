#!/bin/sh

. ./test-common.sh

cleanup 30

# ------------------------------- Test 30 ------------------------------------
# the file with the same date already exists, so it should not be overwritten
# and log should not be rotated
preptest test.log 30 1 0

DATESTRING=$(/bin/date +%Y%m%d)
echo "one" > test.log-$DATESTRING

$RLR test-config.30 --force
checkoutput <<EOF
test.log 0 zero
test.log-$DATESTRING 0 one
EOF
