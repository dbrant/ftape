#!/bin/bash
#
# Dmitry Brant, Sep 2023
# ftape module insertion script, specifically for the
# external (parallel port) Iomega Ditto 2GB drive,
# and/or the Sony StorStation 2GB, which is just a rebranded Iomega.
#

insmod ./ftape-core.ko ft_soft_retries=1 ft_ignore_ecc_err=1 ft_tracings=3,3,3,3,3 ft_fdc_driver=bpck-fdc,none,none,none # ft_fdc_driver=ftape-internal,bpck-fdc:trakker,none,none ft_tracings=3,3,3,3,3
insmod ./zftape.ko # ft_major_device_number=27 # ${27-FT_MAJOR}

# The following used to be necessary, but recent kernels
# auto-load the parport modules as needed. If you have a
# parallel port, it should be auto-detected and available
# through parport, which our modules will use.
# ---------------------------------------
# remove any running instances of parport or related modules
#rmmod lp
#rmmod parport_pc
#rmmod ppdev
#rmmod parport
# and now install our stuff.
#modprobe parport_pc io=0x378 irq=7
#modprobe parport
# ---------------------------------------

insmod ./ftape-parport.ko
insmod ./ftape-bpck.ko

echo "Done."
echo "NOTE: If the modules load successfully, but you still get I/O errors" \
"when trying to read from the drive, make sure your parallel port is" \
"set to EPP in the BIOS, and the correct Port & IRQ numbers are provided" \
"to the parport_pc module above. Check dmesg for more details." \
