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

if [ ! -x "$TEST_ROOTFS/usr/bin/id" ]; then
    $JAIL_CMD install "$TEST_ROOTFS" /usr/bin/id /usr/bin/id || exit 1
fi

expect_stdout "T6.1: run uses a pipe, not a tty" \
    "$JAIL_CMD install $TEST_ROOTFS $FIXTURE_DIR/ttycheck /bin/ttycheck && $JAIL_CMD run $TEST_ROOTFS /bin/ttycheck" <<'EOF'
notty
EOF

expect_stdout "T6.2/T11.2: run and shell drop root before exec" \
    "run_uid=\$($JAIL_CMD run $TEST_ROOTFS /usr/bin/id -u); run_rc=\$?; if [ \"\$run_rc\" -ne 0 ]; then printf 'run failed\n'; elif [ \"\$run_uid\" = 0 ]; then printf 'run root\n'; else printf 'run unprivileged\n'; fi; shell_uid=\$(printf '/usr/bin/id -u\nexit\n' | $JAIL_CMD shell $TEST_ROOTFS /bin/sh | tr -d '\r' | grep -E '^[0-9][0-9]*\$'); shell_rc=\$?; if [ \"\$shell_rc\" -ne 0 ]; then printf 'shell failed\n'; elif [ \"\$shell_uid\" = 0 ]; then printf 'shell root\n'; else printf 'shell unprivileged\n'; fi" <<'EOF'
run unprivileged
shell unprivileged
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
