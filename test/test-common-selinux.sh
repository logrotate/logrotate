# We test if the SELinux tests should be done or not

if [ -z "$SELINUX_TESTS" ]; then
  # not yet checked

  SELINUX_TESTS=0
  if type "selinuxenabled" >/dev/null 2>&1 && selinuxenabled; then
    SELINUX_TESTS=1
  else
    echo "SELinux disabled. SELinux tests will not be executed."
  fi

  if [ $SELINUX_TESTS = 1 ]; then
    # It seems we can run the ACL tests, but was logrotate compiled WITH_ACL=yes ?
    # See the Makefile, "pretest" part, for more information
    import "test.SELINUX"
    if [ -f ./test.SELINUX ]; then
      SELINUX_TESTS=`cat ./test.SELINUX`
      if [ $SELINUX_TESTS = 0 ]; then
        echo "logrotate was NOT compiled with 'WITH_SELINUX=yes'. SELINUX tests will not be executed."
      fi
    fi
  fi

  if [ $SELINUX_TESTS = 1 ]; then
    # if logrotate_tmp_t, we can't continue with SELinux tests...
    touch .selinuxtest
    chcon --type=logrotate_tmp_t .selinuxtest 2>/dev/null
    if [ $? != 0 ]; then
      SELINUX_TESTS=0
      echo "SELinux context 'logrotate_tmp_t' does not exist. SELinux tests will not be executed."
    fi
    rm -f .selinuxtest
  fi
fi
