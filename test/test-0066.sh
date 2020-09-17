#!/bin/sh

. ./test-common.sh

cleanup 66

# ------------------------------- Test 66 ------------------------------------
# When using %Y in the dateformat, the old logs are not removed
preptest test.log 66 1 0

if date -v -1d > /dev/null 2>&1; then
    DAYAGO=$(date -v-1d "+%Y-%m-%d")
else
    DAYAGO=$(date "+%Y-%m-%d" --date "1 day ago")
fi

echo "DAYAGO=${DAYAGO}"

echo removed > "test.log$DAYAGO"

$RLR test-config.66 --force
checkoutput <<EOF
test.log 0
EOF

if [ -f test.log$DAYAGO ]; then
	echo "file $file does exist!"
	exit 2
fi
