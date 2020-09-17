#!/bin/sh

. ./test-common.sh

cleanup 65

# ------------------------------- Test 65 ------------------------------------
# filename in mail's subject without compress directive and maillast directive
# should be the name of the removed log
preptest test.log 65 1 0

DATESTRING=$(/bin/date +%Y%m%d)

$RLR test-config.65 --force
checkoutput <<EOF
test.log 0
EOF

checkmail test.log-$DATESTRING zero
