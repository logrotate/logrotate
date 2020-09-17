#!/bin/sh

. ./test-common.sh

cleanup 64

# ------------------------------- Test 64 ------------------------------------
# filename in mail's subject with compress directive and maillast directive
# should be the name of the removed log
preptest test.log 64 1 0

DATESTRING=$(/bin/date +%Y%m%d)

$RLR test-config.64 --force
checkoutput <<EOF
test.log 0
EOF

checkmail test.log-$DATESTRING.gz zero
