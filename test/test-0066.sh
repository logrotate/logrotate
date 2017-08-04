#!/bin/bash

. ./test-common.sh

cleanup 66

# ------------------------------- Test 66 ------------------------------------
# When using %Y in the dateformat, the old logs are not removed
preptest test.log 66 1 0

DATESTRING=$(/bin/date +%Y%m%d)
DAYAGO=$(/bin/date "+%Y-%m-%d" --date "1 day ago" 2>/dev/null)

echo removed > "test.log$DAYAGO"

$RLR test-config.66 --force
checkoutput <<EOF
test.log 0
EOF

if [ -f test.log$DAYAGO ]; then
	echo "file $file does exist!"
	exit 2
fi
