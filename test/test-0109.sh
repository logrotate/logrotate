#!/bin/sh

. ./test-common.sh

cleanup 109

# ------------------------------- Test 109 ------------------------------------
# Test behavior after prerotate, postrotate, firstaction or lastaction script failure

preptest test-pre.log 109 3
preptest test-post.log 109 3
preptest test-shared-pre-A.log 109 3
preptest test-shared-pre-B.log 109 3
preptest test-shared-post-A.log 109 3
preptest test-shared-post-B.log 109 3
preptest test-first.log 109 3
preptest test-last.log 109 3


checkoutput <<EOF
test-pre.log 0 zero
test-pre.log.1 0 first
test-pre.log.2 0 second
test-pre.log.3 0 third
EOF

checkoutput <<EOF
test-post.log 0 zero
test-post.log.1 0 first
test-post.log.2 0 second
test-post.log.3 0 third
EOF

checkoutput <<EOF
test-shared-pre-A.log 0 zero
test-shared-pre-A.log.1 0 first
test-shared-pre-A.log.2 0 second
test-shared-pre-A.log.3 0 third
EOF

checkoutput <<EOF
test-shared-pre-B.log 0 zero
test-shared-pre-B.log.1 0 first
test-shared-pre-B.log.2 0 second
test-shared-pre-B.log.3 0 third
EOF

checkoutput <<EOF
test-shared-post-A.log 0 zero
test-shared-post-A.log.1 0 first
test-shared-post-A.log.2 0 second
test-shared-post-A.log.3 0 third
EOF

checkoutput <<EOF
test-shared-post-B.log 0 zero
test-shared-post-B.log.1 0 first
test-shared-post-B.log.2 0 second
test-shared-post-B.log.3 0 third
EOF

checkoutput <<EOF
test-first.log 0 zero
test-first.log.1 0 first
test-first.log.2 0 second
test-first.log.3 0 third
EOF

checkoutput <<EOF
test-last.log 0 zero
test-last.log.1 0 first
test-last.log.2 0 second
test-last.log.3 0 third
EOF


$RLR -f test-config.109 && exit 23


checkoutput <<EOF
test-pre.log 0 zero
test-pre.log.1 0 first
test-pre.log.2 0 second
test-pre.log.3 0 third
EOF

if [ -e test-pre.log.4 ]; then
	echo "test-pre.log.4 exists"
	exit 3
fi

checkoutput <<EOF
test-post.log 0
test-post.log.1 0 zero
test-post.log.2 0 first
test-post.log.3 0 second
test-post.log.4 0 third
EOF

checkoutput <<EOF
test-shared-pre-A.log 0 zero
test-shared-pre-A.log.1 0 first
test-shared-pre-A.log.2 0 second
test-shared-pre-A.log.3 0 third
EOF

if [ -e test-shared-pre-A.log.4 ]; then
	echo "test-shared-pre-A.log.4 exists"
	exit 3
fi

checkoutput <<EOF
test-shared-pre-B.log 0 zero
test-shared-pre-B.log.1 0 first
test-shared-pre-B.log.2 0 second
test-shared-pre-B.log.3 0 third
EOF

if [ -e test-shared-pre-B.log.4 ]; then
	echo "test-shared-pre-B.log.4 exists"
	exit 3
fi

checkoutput <<EOF
test-shared-post-A.log 0
test-shared-post-A.log.1 0 zero
test-shared-post-A.log.2 0 first
test-shared-post-A.log.3 0 second
test-shared-post-A.log.4 0 third
EOF

checkoutput <<EOF
test-shared-post-A.log 0
test-shared-post-B.log.1 0 zero
test-shared-post-B.log.2 0 first
test-shared-post-B.log.3 0 second
test-shared-post-B.log.4 0 third
EOF

checkoutput <<EOF
test-first.log 0 zero
test-first.log.1 0 first
test-first.log.2 0 second
test-first.log.3 0 third
EOF

if [ -e test-first.log.4 ]; then
	echo "test-first.log.4 exists"
	exit 3
fi

checkoutput <<EOF
test-last.log 0
test-last.log.1 0 zero
test-last.log.2 0 first
test-last.log.3 0 second
EOF

if [ -e test-last.log.4 ]; then
	echo "test-last.log.4 exists"
	exit 3
fi
