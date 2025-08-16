# ftape

This is a copy of the last official release of the `ftape` driver (version 4.04a) by Claus-Justus Heine et al, with some of my (Dmitry Brant) own tweaks for getting it to compile and run on CentOS 3.5.

DISCLAIMER: I am not a kernel developer, and I barely stumbled through getting this driver to compile and make it work the way I needed. In case anyone wants to learn from my mistakes (and in the spirit of GPL) I'm making this code and my changes available, but don't judge me for the horrible jury-rigging I did to make it work.

## The story so far

Ftape was a driver that allowed Linux to read data from "floppy tape" drives, i.e. tape drives that would connect to the floppy controller (FDC) directly on the motherboard, such as the Colorado Jumbo 250, the Ditto Max, etc. It also supported numerous external tape drives that connected to the parallel port of the PC, such as the Trakker, Iomega Ditto 2GB, etc.

This driver used to be included in the Linux kernel, until it was [removed](https://lwn.net/Articles/202253/) in 2006, as of kernel version 2.6.20.

However, that's not the whole story:
* The last version of the driver that was included in the kernel, right up until it was removed, was version 3.04.
* BUT, the author continued to develop the driver independently of kernel releases. In fact, the last known version of the driver was 4.04a.
* Unfortunately the author continued to develop the driver on an _older_ version of the kernel (probably 2.2.x), which made it somewhat incompatible with later kernel versions.

## Why CentOS 3.5?

This is the oldest distro I could find (installed from [ISO images](https://vault.centos.org/3.5/isos/i386/)) that boots nicely (graphically, with USB support, etc) on my old PC workstation, _and_ has a kernel version of 2.4.21, which brings it just within reach of a horribly amateur kernel hacker to compile `ftape` onto.

With a default install of CentOS 3.5 (all defaults except "software development" and "kernel development" packages selected), I was able to compile `ftape`, load it, and use it to communicate with my tape drives!

## Rough instructions

* Install CentOS from the aforementioned ISO images, making sure to install the "kernel development" package.
* Get this repo somewhere into your home directory on your new CentOS install.
* Make any necessary modifications to MCONFIG. Specifically, you might need to modify the LINUX_LOCATION variable, to point to the source tree of your linux kernel, and the KERNELRELEASE variable to be the exact name of your kernel release.
* Run `make`!

The build process might complain about the `.config` file not being present in your kernel source tree. To fix this, you can copy your current `config...` from your `/boot` partition and name it `.config` in the root of the kernel tree.

When (if) the build process finishes, it should make several kernel modules (`.o` files) in the `modules` subfolder. These are modules that are loadable using `insmod` or `modprobe`. There's actually no need to run `make install` (which copies the modules into your actual `/lib/modules/...` directory), I just prefer to load the modules directly from the build directory on demand.

There is also `scripts/MAKEDEV.ftape` that you should run to create the necessary device nodes under `/dev/`.

Once all of this is done, and the kernel modules get loaded without errors, you can interact with the device files, e.g. `/dev/nqft0`, `/dev/rawqft0` and so on.

For extra convenience, there are scripts in the `modules/` directory for loading the modules with the proper parameters for specific types of drives (look inside these scripts for examples and explanations of the parameters):
* `insert_floppy` for when you have a drive connected to the floppy controller (FDC), such as Colorado 250, etc.
* `insert_max` for when you have a Ditto Max drive, specifically, connected to the floppy controller.
* `insert_parallel` for when you have a drive connected to the parallel port. This script might show warnings when loading, but ignore them and check `dmesg` for the true state of the driver. (And make sure your parallel port is configured for ECP in your BIOS.)
