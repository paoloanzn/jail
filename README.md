# jail

SIP-friendly macOS micro-containers for modern Apple Silicon.

Run an isolated container-like environment without docker, linux vm and without disabling SIP(System Integrity Protection)!

## How to use jail

1. Install `jail`
```zsh
git clone https://github.com/paoloanzn/jail.git
sudo make install # /usr/local/bin
```

2. Bootstrap a minimal fsroot
```zsh
sudo jail bootstrap ./rootfs
```

3. Install a bash shell and run it
```zsh
sudo jail install ./rootfs /bin/bash /bin/bash # <host-binary-path> <rootfs-relative-binary-path>
sudo jail shell ./roofs /bin/bash
```

```bash
paolo@Mac /Users/paolo/Fun/jail 0>sudo jail shell rootfs /bin/bash
bash-3.2# printf 'Hello from jail!\n'; ls -lah
Hello from jail!
total 8
drwxr-xr-x@ 6 root  staff   192B May 23 19:39 .
drwxr-xr-x@ 6 root  staff   192B May 23 19:39 ..
drwxr-xr-x@ 3 root  staff    96B May 23 15:32 System
drwxr-xr-x@ 5 root  staff   160B May 23 19:37 bin
-rw-r--r--@ 1 root  staff    27B May 23 19:24 test.txt
drwxr-xr-x@ 4 root  staff   128B May 23 17:47 usr
bash-3.2#
```

Congrats you have been jailed! You can then install any other binary you want.

## Roadmap

See [ROADMAP.md](ROADMAP.md) for project direction and planned areas of work.

## License

Apache-2.0.
