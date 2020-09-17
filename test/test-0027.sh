#!/bin/sh

. ./test-common.sh

cleanup 27

# ------------------------------- Test 27 ------------------------------------
# logrotate fails to find the correct file to mail, when using "mailfirst" in
# combination with "delaycompress" and "dateext" option.
preptest test.log 27 1 0

DATESTRING=$(/bin/date +%Y%m%d)

$RLR test-config.27 --force
checkoutput <<EOF
test.log 0
test.log-$DATESTRING 0 zero
EOF

checkmail test.log-$DATESTRING zero
