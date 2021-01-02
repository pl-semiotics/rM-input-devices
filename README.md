# Introduction

This library provides a simple API for receiving and sending
digitizer/touch/keyboard events on the [reMarkable
tablets](https://remarkable.com). Currently, the main consumer is
[rM-vnc-server](https://github.com/pl-semiotics/rM-vnc-server). It
also supports using uinput to emulate the presence of such devices,
which may be useful in virtualized environments.

# Building

The supported way to build this is via the
[Nix](https://nixos.org/nix) package manager, through the
[nix-remarkable](https://github.com/pl-semiotics/nix-remarkable)
expressions. To build just this project via `nix build` from this
repo, download it into the `pkgs/` directory of `nix-remarkable`.

For any other system, the [Makefile](./Makefile) should provide some
guidance; please set `REMARKABLE_VERSION` to one or two (depending on
which version you are building for), and, if interested in the
standalone (statically linked, with a bundled uinput kernel module)
version of the library, provide the path to appropriate kernel module
in the `UINPUT_KO` environment variable.

Prebuilt binaries are available in the [Releases
tab](https://github.com/pl-semiotics/rM-input-devices/releases).

# Usage

For the library, see [rM-input-devices.h](./rM-input-devices.h) for
the interface. A simple executable `rM-mk-uinput` (which takes no
arguments and does nothing but create uinput devices for any missing
input devices) is also provided.
