#!/bin/sh

expect_stdout "T5.1/T5.2: chroot path visibility" \
    "$JAIL_CMD install $TEST_ROOTFS $FIXTURE_DIR/peek /bin/peek && $JAIL_CMD run $TEST_ROOTFS /bin/peek" <<'EOF'
SEE bin/peek
MISS Users
SEE dyld
EOF
