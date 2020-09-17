#!/bin/sh

. ./test-common.sh

cleanup 76

# ------------------------------- Test 76 ------------------------------------
# compress and mail should work when logrotate runs with closed stdin/stdout
# https://github.com/logrotate/logrotate/issues/154
preptest test.log 76 2 2

$RLR test-config.76 <&- >&-

checkoutput <<EOF
test.log 0
test.log.1.gz 1 zero
test.log.2.gz 1 first
EOF

checkmail test.log.3.gz second
