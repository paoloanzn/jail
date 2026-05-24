#!/bin/sh

expect_stdout "T2.1: bootstrap idempotent" \
    "$JAIL_CMD bootstrap $TEST_ROOTFS && $JAIL_CMD bootstrap $TEST_ROOTFS; printf '%s\n' \"\$?\"" <<'EOF'
0
EOF

expect_stdout "T2.2: bootstrap top-level directories" \
    "LC_ALL=C ls -1 $TEST_ROOTFS" <<'EOF'
System
bin
usr
EOF
