#!/bin/sh

expect_stdout "T2.1: bootstrap idempotent" \
    "$JAIL_CMD bootstrap $TEST_ROOTFS && $JAIL_CMD bootstrap $TEST_ROOTFS; printf '%s\n' \"\$?\"" <<'EOF'
0
EOF

expect_stdout "T2.2: bootstrap top-level directories" \
    "LC_ALL=C ls -1 $TEST_ROOTFS" <<'EOF'
System
bin
tmp
usr
EOF

expect_stdout "T2.3: bootstrap installs default /bin tools" \
    "for tool in '[' bash cat chmod cp csh dash date dd df echo ed expr hostname kill ksh launchctl link ln ls mkdir mv pax ps pwd realpath rm rmdir sh sleep stty sync tcsh test unlink wait4path zsh; do path=$TEST_ROOTFS/bin/\$tool; if [ ! -f \"\$path\" ] || [ ! -x \"\$path\" ]; then printf 'not executable: %s\n' \"\$tool\"; fi; done; LC_ALL=C ls -1 $TEST_ROOTFS/bin" <<'EOF'
[
bash
cat
chmod
cp
csh
dash
date
dd
df
echo
ed
expr
hostname
kill
ksh
launchctl
link
ln
ls
mkdir
mv
pax
ps
pwd
realpath
rm
rmdir
sh
sleep
stty
sync
tcsh
test
unlink
wait4path
zsh
EOF

expect_stdout "T2.4: bootstrap installs default /usr/bin tools" \
    "for tool in awk basename bsdtar bunzip2 bzip2 cksum cmp comm cut diff diff3 dirname du egrep env expand false fgrep find fold grep groups gunzip gzip head hexdump id join jot mktemp nice nohup od paste patch printf sed seq sort split stat sum tail tar tee time touch tr true tty uname unexpand uniq unzip wc whoami xargs xxd yes zip; do path=$TEST_ROOTFS/usr/bin/\$tool; if [ ! -f \"\$path\" ] || [ ! -x \"\$path\" ]; then printf 'not executable: %s\n' \"\$tool\"; fi; done; LC_ALL=C ls -1 $TEST_ROOTFS/usr/bin" <<'EOF'
awk
basename
bsdtar
bunzip2
bzip2
cksum
cmp
comm
cut
diff
diff3
dirname
du
egrep
env
expand
false
fgrep
find
fold
grep
groups
gunzip
gzip
head
hexdump
id
join
jot
mktemp
nice
nohup
od
paste
patch
printf
sed
seq
sort
split
stat
sum
tail
tar
tee
time
touch
tr
true
tty
uname
unexpand
uniq
unzip
wc
whoami
xargs
xxd
yes
zip
EOF
