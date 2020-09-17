#!/bin/sh

. ./test-common.sh

cleanup 46

# ------------------------------- Test 46 ------------------------------------
# the state file is truncated and obviously corrupt
preptest test.log 46 1

cat > state << EOF
logrotate state -- version 1
"$PWD/test.log" 2000-1-1
"$PWD/test2.l
EOF

DATESTRING=$(/bin/date +%Y%m%d)
$RLR test-config.46 2>error.log

grep "error: bad line 3 in state file state" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi

rm -f error.log

checkoutput <<EOF
test.log 0
EOF


. ./test-common-selinux.sh
if [ $SELINUX_TESTS = 1 ]; then
	chcon --type=logrotate_tmp_t test.log
else
	echo "Skipping SELinux part of test 46"
fi
