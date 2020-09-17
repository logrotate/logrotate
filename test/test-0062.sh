#!/bin/sh

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
if [ $SIZE_SPARSE_OLD -gt 100 ]; then
    echo "unable to create (or detect) sparse files"
    exit 77
fi

SIZE_OLD=$($DU_APPARENT_SIZE test.log|awk '{print $1}')
$RLR test-config.62 --force
SIZE_NEW=$($DU_APPARENT_SIZE test.log.1|awk '{print $1}')
SIZE_SPARSE_NEW=$(du test.log.1|awk '{print $1}')

if [ $SIZE_OLD != $SIZE_NEW ]; then
	echo "Bad apparent size of sparse logs"
	echo "test.log: $SIZE_OLD"
	echo "test.log.1: $SIZE_NEW"
	exit 3
fi

if [ $SIZE_SPARSE_NEW -gt 100 ]; then
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
