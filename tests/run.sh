#!/bin/sh

set -u

# Resolve paths from this file so the runner can be invoked from any cwd.
script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd) || exit 1
ROOT_DIR=$(CDPATH= cd "$script_dir/.." && pwd) || exit 1
TEST_DIR=$script_dir

# These can be overridden by the caller, e.g. CC=clang or SUDO="sudo -n".
JAIL_BIN=$ROOT_DIR/jail
CC=${CC:-cc}
SUDO=${SUDO:-sudo}

if [ "$(uname -s)" != "Darwin" ]; then
    printf '%s\n' "jail tests require macOS" >&2
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
    SUDO=
else
    # Validate credentials once up front instead of failing mid-suite.
    if ! sh -c "$SUDO -v"; then
        printf '%s\n' "jail tests require root privileges via sudo" >&2
        exit 1
    fi
fi

if [ -n "$SUDO" ]; then
    JAIL_CMD="$SUDO $JAIL_BIN"
else
    JAIL_CMD=$JAIL_BIN
fi

# Use a fresh disposable rootfs by default. Set JAIL_TEST_WORKDIR to reuse a
# specific directory, or JAIL_TEST_KEEP=1 to inspect a failing run afterward.
TEST_WORKDIR=${JAIL_TEST_WORKDIR:-}
own_workdir=0
if [ -z "$TEST_WORKDIR" ]; then
    tmpbase=${TMPDIR:-/tmp}
    TEST_WORKDIR=$(mktemp -d "$tmpbase/jail-tests.XXXXXX") || exit 1
    own_workdir=1
else
    mkdir -p "$TEST_WORKDIR" || exit 1
fi

TEST_ROOTFS=$TEST_WORKDIR/rootfs
FIXTURE_DIR=$TEST_WORKDIR/fixtures

tests_run=0
tests_failed=0

cleanup() {
    status=$?
    # The copied dyld cache and installed binaries are root-owned in normal
    # runs, so cleanup must use the same privilege path as the tests.
    if [ "${JAIL_TEST_KEEP:-0}" = "1" ] || [ "$own_workdir" -ne 1 ]; then
        printf 'kept test workdir: %s\n' "$TEST_WORKDIR"
    else
        if [ -n "$SUDO" ]; then
            $SUDO rm -rf "$TEST_WORKDIR"
        else
            rm -rf "$TEST_WORKDIR"
        fi
    fi
    exit "$status"
}
trap cleanup 0

print_indented_file() {
    file=$1
    while IFS= read -r line || [ -n "$line" ]; do
        printf '  %s\n' "$line"
    done < "$file"
}

expect_stdout() {
    desc=$1
    command=$2
    tests_run=$((tests_run + 1))
    case_id=$(printf '%03d' "$tests_run")
    expected_file=$TEST_WORKDIR/expected.$case_id
    stdout_file=$TEST_WORKDIR/stdout.$case_id
    stderr_file=$TEST_WORKDIR/stderr.$case_id

    cat > "$expected_file"

    # test format: print the command, capture stdout, and
    # compare it byte-for-byte with the heredoc expected output.
    printf '# %s\n' "$desc"
    printf '$ %s\n' "$command"

    ( cd "$ROOT_DIR" && sh -c "$command" ) > "$stdout_file" 2> "$stderr_file"
    command_status=$?

    if [ "$command_status" -ne 0 ]; then
        tests_failed=$((tests_failed + 1))
        printf 'not ok %s - %s (exit %s)\n' "$case_id" "$desc" "$command_status"
        printf 'expected stdout:\n'
        print_indented_file "$expected_file"
        printf 'actual stdout:\n'
        print_indented_file "$stdout_file"
        if [ -s "$stderr_file" ]; then
            printf 'actual stderr:\n'
            print_indented_file "$stderr_file"
        fi
        return 0
    fi

    if cmp -s "$expected_file" "$stdout_file"; then
        printf 'ok %s - %s\n' "$case_id" "$desc"
        return 0
    fi

    tests_failed=$((tests_failed + 1))
    printf 'not ok %s - %s\n' "$case_id" "$desc"
    if command -v diff >/dev/null 2>&1; then
        diff -u "$expected_file" "$stdout_file" || true
    else
        printf 'expected stdout:\n'
        print_indented_file "$expected_file"
        printf 'actual stdout:\n'
        print_indented_file "$stdout_file"
    fi
    if [ -s "$stderr_file" ]; then
        printf 'actual stderr:\n'
        print_indented_file "$stderr_file"
    fi
    return 0
}

build_runner() {
    printf 'building jail\n'
    ( cd "$ROOT_DIR" && $CC -o jail jail.c ) || exit 1
}

build_fixtures() {
    mkdir -p "$FIXTURE_DIR" || exit 1

    # Keep fixtures as sh scripts so the test suite itself stays shell-only.
    # /bin/sh and its /bin/bash variant are installed by the tests before use.
    cat > "$FIXTURE_DIR/hello" <<'EOF'
#!/bin/sh
printf 'hello, world\n'
EOF
    chmod +x "$FIXTURE_DIR/hello" || exit 1

    cat > "$FIXTURE_DIR/count" <<'EOF'
#!/bin/sh
for i in 1 2 3 4 5; do
    printf '%s\n' "$i"
done
EOF
    chmod +x "$FIXTURE_DIR/count" || exit 1

    cat > "$FIXTURE_DIR/greet" <<'EOF'
#!/bin/sh
name=${1:-world}
printf 'hello, %s!\n' "$name"
EOF
    chmod +x "$FIXTURE_DIR/greet" || exit 1

    cat > "$FIXTURE_DIR/peek" <<'EOF'
#!/bin/sh
if [ -e /bin/peek ]; then printf 'SEE bin/peek\n'; else printf 'MISS bin/peek\n'; fi
if [ -e /Users ]; then printf 'SEE Users\n'; else printf 'MISS Users\n'; fi
if [ -e /usr/lib/dyld ]; then printf 'SEE dyld\n'; else printf 'MISS dyld\n'; fi
EOF
    chmod +x "$FIXTURE_DIR/peek" || exit 1

    cat > "$FIXTURE_DIR/ttycheck" <<'EOF'
#!/bin/sh
if [ -t 0 ]; then
    printf 'tty\n'
else
    printf 'notty\n'
fi
EOF
    chmod +x "$FIXTURE_DIR/ttycheck" || exit 1
}

build_runner
build_fixtures

# With no arguments, run every numbered test file. Passing file paths runs only
# that subset, which is useful while developing a new regression test.
if [ "$#" -eq 0 ]; then
    set -- "$TEST_DIR"/[0-9][0-9][0-9]-*.sh
fi

for test_file in "$@"; do
    if [ ! -f "$test_file" ]; then
        printf 'missing test file: %s\n' "$test_file" >&2
        exit 1
    fi
    # Test files are sourced so they share the harness helpers and paths.
    . "$test_file"
done

printf '%s tests, %s failures\n' "$tests_run" "$tests_failed"
[ "$tests_failed" -eq 0 ]
