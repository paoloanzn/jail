#!/bin/sh

expect_stdout "T8.1: installed /bin/ls loses hardened runtime" \
    "$JAIL_CMD install $TEST_ROOTFS /bin/ls /bin/ls && codesign -dvv $TEST_ROOTFS/bin/ls >$TEST_WORKDIR/ls.codesign 2>&1 && if grep -q '0x10000' $TEST_WORKDIR/ls.codesign; then printf 'runtime present\n'; else printf 'runtime absent\n'; fi" <<'EOF'
runtime absent
EOF

expect_stdout "T8.2: /bin/ls survives three consecutive runs" \
    "$JAIL_CMD run $TEST_ROOTFS /bin/ls -1 /; printf 'rc=%s\n' \"\$?\"; $JAIL_CMD run $TEST_ROOTFS /bin/ls -1 /; printf 'rc=%s\n' \"\$?\"; $JAIL_CMD run $TEST_ROOTFS /bin/ls -1 /; printf 'rc=%s\n' \"\$?\"" <<'EOF'
System
bin
tmp
usr
rc=0
System
bin
tmp
usr
rc=0
System
bin
tmp
usr
rc=0
EOF
