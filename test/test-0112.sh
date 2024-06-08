#!/bin/sh

. ./test-common.sh

cleanup 112

# ------------------------------- Test 112 -------------------------------------
# Do not hang on a named pipe (FIFO)

preptest test_reg.log 112 2 1
mkfifo test_fifo.log

if [ ! -p test_fifo.log ]; then
    echo "FIFO file test_fifo.log should exist"
    exit 3
fi

$RLR --force test-config.112 2>error.log && exit 23

checkoutput <<EOF
test_reg.log 0
test_reg.log.1.gz 1 zero
test_reg.log.2.gz 1 first
EOF

grep "^error: unable to open .*/test_fifo\.log\.1 (read-only) for compression: " error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi

if [ ! -f test_fifo.log ]; then
    echo "Regular file test_fifo.log should exist"
    exit 3
fi

if [ ! -p test_fifo.log.1 ]; then
    echo "FIFO file test_fifo.log.1 should exist"
    exit 3
fi
