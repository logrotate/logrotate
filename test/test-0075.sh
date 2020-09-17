#!/bin/sh

. ./test-common.sh

cleanup 75

# ------------------------------- Test 75 ------------------------------------
# uncompress logs before mailing them even if delaycompress is enabled
# https://github.com/logrotate/logrotate/issues/151
preptest test.log 75 2 1

$RLR test-config.75

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test.log.2.gz 1 first
EOF

checkmail test.log.3.gz second
