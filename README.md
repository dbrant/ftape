# ftape

This is a continuation of the development of the `ftape` driver, originally by Claus-Justus Heine et al, with some of [my](https://dmitrybrant.com) (Dmitry Brant) own tweaks and modernizations for getting it to compile and run on modern Linux kernels (version 6.8 and higher, in 2025).

TLDR: It absolutely works! (*on my system running Xubuntu 24.04 (64-bit!)) However, there is still a lot of testing to be done, and therefore there may still be bugs related to the modernization effort. For my previous work getting the completely original (unmodified) driver to work with CentOS 3.5 (with kernel version 2.4.21), refer to the `centos35` [branch](/tree/centos35) of this repo, and the README therein.

## The story so far

Ftape was a driver that allowed Linux to read data from "floppy tape" drives, i.e. tape drives that would connect to the floppy controller (FDC) directly on the motherboard, such as the Colorado Jumbo 250, the Ditto Max, etc. It also supported numerous external tape drives that connected to the parallel port of the PC, such as the Trakker, Iomega Ditto 2GB, etc.

This driver used to be included in the Linux kernel, until it was [removed](https://lwn.net/Articles/202253/) in 2006, as of kernel version 2.6.20.

However, that's not the whole story:
* The last version of the driver that was included in the kernel, right up until it was removed, was version 3.04.
* BUT, the author continued to develop the driver independently of kernel releases. In fact, the last known version of the driver was 4.04a, in 2000.
* My goal is to continue maintaining this driver for modern kernel versions, 25 years after the last official release.

## Motivation

Even though QIC tapes have long been obsolete as a backup medium, there is still a nontrivial market for _data recovery_ from such tapes that still exist in the wild, where the ftape driver can fill an important niche. There is also the retrocomputing community, which can benefit from such a driver bringing old hardware back to life.

## Goals

* Since this driver is intended mostly for recovering data from old cartridges, more emphasis will be placed on maintaining _reading_ functionality rather than writing, emphasizing compatibility with various tape drives. Bugs related to _writing_ may be deprioritized, and indeed I might consider removing writing/formatting functionality entirely, and leave it in a separate unmaintained branch.
* In the interest of making this driver into a "forensic"-quality recovery tool, more emphasis will be placed on lower-level data extraction, e.g. raw mode, disregarding volume tables, disregarding ECC failures, etc.
* The intention is to compile this driver as an out-of-tree kernel module, without needing to copy it into the kernel source tree. That's why there's just a simple Makefile, and no other affordances for kernel inclusion. I can't really imagine any further need to build this driver into the kernel itself.

## Rough instructions

* Make sure your build tools are installed: `sudo apt install build-essential`
* Clone this repo.
* Run `make`!

When (if) the build process finishes, it should make several kernel modules (`.ko` files) in the base folder of the repo. These are modules that are loadable using `insmod` or `modprobe`. There's actually no need to run `make install` (which copies the modules into your actual `/lib/modules/...` directory), I just prefer to load the modules directly from the repo directory on demand.

For convenience, there are scripts for loading the modules in the proper order, with the proper parameters for specific types of drives (look inside these scripts for examples and explanations of the parameters):
* `insert_floppy.sh` for when you have a drive connected to the floppy controller (FDC), such as Colorado 250, etc.
* `insert_max.sh` for when you have a Ditto Max drive, specifically, connected to the floppy controller.
* `insert_parallel.sh` for when you have a drive connected to the parallel port. This script might show warnings when loading, but ignore them and check `dmesg` for the true state of the driver. (And make sure your parallel port is configured for ECP in your BIOS.)

Once all of this is done, and the kernel modules are loaded without errors, you can interact with the tape device files, e.g. `/dev/nqft0`, `/dev/rawqft0` and so on. You can proceed to dump the data from a tape using a command like: `dd if=/dev/nrawqft0 of=out.bin bs=10240`

## Troubleshooting

Make generous use of `dmesg` and look at the log messages therein. Use the `ft_tracings` module parameter to increase the verbosity of the messages to narrow down any issues.

## Disclaimers

* This driver should work on either 32-bit or 64-bit x86 architecture, but obviously it still only works with the same limited set of FDC chipsets as before. If you want to use it with a real FDC tape drive, you must connect the drive to a motherboard with a supported FDC.
* I have a rather [limited collection](https://dmitrybrant.com/inventory-media) of tape drives for testing and experimenting, so I make no guarantees about the driver working with your system. If you have a drive that doesn't seem to work with this driver, or if you simply need to recover data from QIC tapes, get in touch!
