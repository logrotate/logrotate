#!/bin/bash

. ./test-common.sh

cleanup 62

# ------------------------------- Test 62 ------------------------------------
# Rotate sparse file
preptest test.log 62 1 0

printf zero > test.log
truncate -s 10M test.log
echo x >> test.log

cp test.log test.example

SIZE_SPARSE_OLD=$(du test.log|awk '{print $1}')
SIZE_OLD=$(du --apparent-size test.log|awk '{print $1}')
$RLR test-config.62 --force
SIZE_NEW=$(du --apparent-size test.log.1|awk '{print $1}')
SIZE_SPARSE_NEW=$(du test.log.1|awk '{print $1}')

if [ $SIZE_OLD != $SIZE_NEW ]; then
	echo "Bad apparent size of sparse logs"
	echo "test.log: $SIZE_OLD"
	echo "test.log.1: $SIZE_NEW"
	exit 3
fi

PAGESIZE="$(getconf PAGESIZE)"
if [ -z "$PAGESIZE" ] || [ "$PAGESIZE" -lt 32768 ] ; then
	LIMIT1=100
	LIMIT2=100
else
	LIMIT1=200
	LIMIT2=20000
fi
if [ $SIZE_SPARSE_OLD -gt $LIMIT1 ] || [ $SIZE_SPARSE_NEW -gt $LIMIT2 ]; then
	echo "Bad size of sparse logs"
	echo "test.log: $SIZE_SPARSE_OLD"
	echo "test.log.1: $SIZE_SPARSE_NEW"
	exit 3
fi

checkoutput <<EOF
test.log 0
test.log.1 0 zerox
EOF

rm -f test.example
