#!/bin/sh

. ./test-common.sh

cleanup 23

# ------------------------------- Test 23 ------------------------------------
# symlinks - symlinks rotation is not allowed for security reasons.
preptest test.log.original 23 1
ln -s test.log.original test.log
$RLR test-config.23 --force 2>error.log

checkoutput <<EOF
test.log 0 zero
test.log.original 0 zero
EOF

rm -f test.log 2>/dev/null || true
