# jail

SIP-friendly macOS micro-containers for modern Apple Silicon.

Run an isolated container-like environment without docker, linux vm and without disabling SIP(System Integrity Protection)!

## How to use jail

1. Install `jail`
```
git clone https://github.com/paoloanzn/jail.git
sudo make install # /usr/local/bin
```

2. Bootstrap a minimal fsroot
```
sudo jail bootstrap ./rootfs
```

3. Install a bash shell and run it
```
sudo jail install ./rootfs /bin/bash /bin/bash # <host-binary-path> <rootfs-relative-binary-path>
sudo jail shell ./roofs /bin/bash
```

Congrats you have been jailed! You can then install any other binary you want.

## Roadmap

See [ROADMAP.md](ROADMAP.md) for project direction and planned areas of work.

## License

Apache-2.0.
