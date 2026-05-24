#!/bin/sh

expect_stdout "T2.3: nonexistent binary returns nonzero" \
    "$JAIL_CMD run $TEST_ROOTFS /bin/nope; printf '%s\n' \"\$?\"" <<'EOF'
jail: execve: No such file or directory
1
EOF
