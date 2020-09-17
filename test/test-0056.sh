#!/bin/sh

. ./test-common.sh

cleanup 56

# ------------------------------- Test 56 ------------------------------------
# removing last log file when using %d-%m-%Y
rm -f *test.log*
preptest test.log 56 1 0

DATE=""
for i in $(seq 1 60)
do
    if date -v -1d > /dev/null 2>&1; then
        DATE=$(date -v-${i}d "+%d-%m-%Y")
    else
        DATE=$(date "+%d-%m-%Y" --date "$i day ago")
    fi
    echo "x" > test.log-$DATE
done

$RLR test-config.56 --force

if [ -e test.log-$DATE ]; then
    echo "File test.log-$DATE should not exist (it should be deleted)"
    exit 3
fi

rm -f *test.log*
