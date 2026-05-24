#!/bin/sh

expect_stdout "T8.1: installed /bin/sh loses hardened runtime" \
    "$JAIL_CMD install $TEST_ROOTFS /bin/sh /bin/sh && $JAIL_CMD install $TEST_ROOTFS /bin/bash /bin/bash && codesign -dvv $TEST_ROOTFS/bin/sh >$TEST_WORKDIR/sh.codesign 2>&1 && if grep -q '0x10000' $TEST_WORKDIR/sh.codesign; then printf 'runtime present\n'; else printf 'runtime absent\n'; fi" <<'EOF'
runtime absent
EOF

expect_stdout "T3.1: install + run round-trip" \
    "$JAIL_CMD install $TEST_ROOTFS $FIXTURE_DIR/hello /bin/hello && $JAIL_CMD run $TEST_ROOTFS /bin/hello" <<'EOF'
hello, world
EOF

expect_stdout "T3.2: multiple binaries in one rootfs" \
    "$JAIL_CMD install $TEST_ROOTFS $FIXTURE_DIR/count /bin/count && $JAIL_CMD run $TEST_ROOTFS /bin/count" <<'EOF'
1
2
3
4
5
EOF

expect_stdout "T4.2: arguments passed through" \
    "$JAIL_CMD install $TEST_ROOTFS $FIXTURE_DIR/greet /usr/bin/greet && $JAIL_CMD run $TEST_ROOTFS /usr/bin/greet Sonoma" <<'EOF'
hello, Sonoma!
EOF

expect_stdout "T2.2: rootfs reusable across runs" \
    "$JAIL_CMD run $TEST_ROOTFS /bin/hello && $JAIL_CMD run $TEST_ROOTFS /bin/hello" <<'EOF'
hello, world
hello, world
EOF
