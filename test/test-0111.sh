#!/bin/sh

. ./test-common.sh

cleanup 111

# ------------------------------- Test 111 ------------------------------------
# test %z dateformat specifier
preptest test.log 111 0

echo foo > test.log.2000-01-01+0100
echo bar > test.log.2001-01-01-1200

$RLR test-config.111 --force || exit 23

DATESTRING=$(/bin/date +%Y-%m-%d%z)

checkoutput <<EOF
test.log 0
test.log.$DATESTRING 0 zero
test.log.2001-01-01-1200 0 bar
EOF

if [ -e test.log.2000-01-01+0100 ]; then
	echo "test.log.2000-01-01+0100"
	exit 3
fi
