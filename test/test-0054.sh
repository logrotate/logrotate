#!/bin/sh

. ./test-common.sh

cleanup 54

# ------------------------------- Test 54 ------------------------------------
# removing last log file when using %Y-%m-%d
rm -f *test.log*
preptest test.log 54 1 0

DATE=""
for i in $(seq 1 60)
do
    if date -v -1d > /dev/null 2>&1; then
        DATE=$(date -v-${i}d "+%Y-%m-%d")
    else
        DATE=$(date "+%Y-%m-%d" --date "$i day ago")
    fi
    echo "x" > test.log-$DATE
done

$RLR test-config.54 --force

if [ -e test.log-$DATE ]; then
    echo "File test.log-$DATE should not exist (it should be deleted)"
    exit 3
fi

rm -f *test.log*
