#!/bin/sh

. ./test-common.sh

# we don't want any stuff left from previous runs
cleanup 91
rm -Rf real*.log*

# ------------------------------- Test 91 ------------------------------------
# rotate file with multiple hard links when enabled
preptest real.log 91 0
ln real.log test.log

$RLR -f test-config.91 || exit 23

checkoutput <<EOF
test.log 0
test.log.1 0 zero
real.log 0
EOF
