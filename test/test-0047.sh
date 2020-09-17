#!/bin/sh

. ./test-common.sh

cleanup 47

. ./test-common-selinux.sh
if [ $SELINUX_TESTS = 0 ]; then
	echo "Skipping SELinux test 47"
	exit 77
fi

# ------------------------------- Test 47 ------------------------------------
# test that newly created state file has the same SELinux context as the
# previous one
preptest test.log 47 1

cat > state << EOF
logrotate state -- version 2
EOF

chcon --type=logrotate_tmp_t state

$RLR test-config.47

ls -Z state|grep logrotate_tmp_t >/dev/null
if [ $? != 0 ]; then
	echo "state file should have selinux context logrotate_tmp_t."
	exit 3
fi


checkoutput <<EOF
test.log 0 zero
EOF
