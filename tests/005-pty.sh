#!/bin/sh

expect_stdout "T6.1: run uses a pipe, not a tty" \
    "$JAIL_CMD install $TEST_ROOTFS $FIXTURE_DIR/ttycheck /bin/ttycheck && $JAIL_CMD run $TEST_ROOTFS /bin/ttycheck" <<'EOF'
notty
EOF

expect_stdout "T11.1: shell gives child a tty" \
    "( printf '/bin/ttycheck\nexit\n'; sleep 1 ) | $JAIL_CMD shell $TEST_ROOTFS /bin/sh | tr -d '\r' | grep '^tty$'" <<'EOF'
tty
EOF
