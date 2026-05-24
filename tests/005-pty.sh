#!/bin/sh

if [ ! -x "$TEST_ROOTFS/usr/lib/dyld" ]; then
    $JAIL_CMD bootstrap "$TEST_ROOTFS" || exit 1
fi

if [ ! -x "$TEST_ROOTFS/bin/sh" ]; then
    $JAIL_CMD install "$TEST_ROOTFS" /bin/sh /bin/sh || exit 1
fi

if [ ! -x "$TEST_ROOTFS/bin/bash" ]; then
    $JAIL_CMD install "$TEST_ROOTFS" /bin/bash /bin/bash || exit 1
fi

expect_stdout "T6.1: run uses a pipe, not a tty" \
    "$JAIL_CMD install $TEST_ROOTFS $FIXTURE_DIR/ttycheck /bin/ttycheck && $JAIL_CMD run $TEST_ROOTFS /bin/ttycheck" <<'EOF'
notty
EOF

expect_stdout "T11.1: shell gives child a tty" \
    "( printf '/bin/ttycheck\nexit\n'; sleep 1 ) | $JAIL_CMD shell $TEST_ROOTFS /bin/sh | tr -d '\r' | grep '^tty$'" <<'EOF'
tty
EOF

expect_stdout "T13.1: final output line is not lost" \
    "printf 'echo final-line\nexit\n' | $JAIL_CMD shell $TEST_ROOTFS /bin/sh | tr -d '\r' | grep -xc 'final-line'" <<'EOF'
1
EOF

expect_stdout "T13.2: closing stdin terminates the shell cleanly" \
    ": | $JAIL_CMD shell $TEST_ROOTFS /bin/sh >/dev/null; printf '%s\n' \"\$?\"" <<'EOF'
0
EOF

expect_stdout "T13.3: large output drains" \
    "printf 'i=1\nwhile [ \"\$i\" -le 1000 ]; do echo \"line \$i\"; i=\$((i + 1)); done\nexit\n' | $JAIL_CMD shell $TEST_ROOTFS /bin/sh | tr -d '\r' | grep -c '^line [0-9][0-9]*$'" <<'EOF'
1000
EOF
