#!/bin/sh

. ./test-common.sh

cleanup 74

# ------------------------------- Test 74 ------------------------------------
# unlink of log file that no longer exists should be handled as a warning only
# https://github.com/logrotate/logrotate/issues/144
preptest test.log 74 1

$RLR test-config.74

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
