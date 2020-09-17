#!/bin/sh

. ./test-common.sh

cleanup 67

# ------------------------------- Test 67 ------------------------------------
# firstaction and lastaction scripts should be called if no file is rotated
preptest test.log 67 1 0

DATESTRING=$(/bin/date +%Y%m%d)
TODAY=$(/bin/date "+%Y-%m-%d" 2>/dev/null)

echo removed > "test.log$TODAY"

$RLR test-config.67 --force

cat scriptout|grep firstaction >/dev/null
if [ $? != 0 ]; then
	echo "scriptout should contain 'firstaction'"
	exit 3
fi

cat scriptout|grep lastaction >/dev/null
if [ $? != 0 ]; then
	echo "scriptout should contain 'lastaction'"
	exit 3
fi
