#!/bin/bash
#
# Example module insertion script for ftape-4.x
#
# Please modify to reflect your hardware configuration
#
# isapnp ./isapnp.conf
insmod ./ftape-core.ko ft_soft_retries=1 ft_ignore_ecc_err=1 ft_tracings=5,5,5,5,5 # ft_fdc_driver=ftape-internal,bpck-fdc:trakker,none,none ft_tracings=3,3,3,3,3
insmod ./zftape.ko # ft_major_device_number=27 # ${27-FT_MAJOR}
#
# NOTE: YOU DON'T NEED zft-compressor.o UNLESS you want to decompress
# compressed volumes created by ftape-3.x. Writing of compressed
# volumes is no longer supported.
#
# insmod ./zft-compressor.o
#
# NOTE: even the newest modules utilities are broken like hell with
# 2.0 kernels. Try to specify a negative paramenter, i.e. -1, and it
# is interpreted as a string. Try to say: 0xffffffff, and NO, you
# DON'T get -1, but MAXINT. :-(
#
# NOTE: you can mess up things like hell if you specify wrong
# parameters. Specifically, don't use the rate limit parameter if
# everything works without it. Valid setting for "rate_limit" are
# 4000, 3000, 2000, 1000 and 500 (500 is pretty useless).
# 
# If you have a Ditto Max tape drive, DO NOT attempt to specify a rate
# limit below 2000Kbps! It _really_ needs at least 2000Kbps.
#
# TR-3 tape drives need at least 1000Kbps.
#
# QIC-80 tape drives need at least 500Kbps.
#
# QIC-40 tape drives work at 250Kbps.
#
# PLEASE MAKE SURE neither to forget any of the "0"s when specifying
# the rate limit, nor to add an additional "0". There is really much
# of a difference between "500" and "5000", or, even worse, "2000" and
# "200".
#
# I'll creep through your network interface and ask General Failure to
# read your hard disk if you mess up the ft_fdc_rate_limit parameter
# by messing up the number of "0"s!
#

insmod ./ftape-internal.ko # ft_fdc_fc10=0 ft_fdc_mach2=0 ft_fdc_base=0x210 ft_fdc_dma=0 ft_fdc_threshold=15 # ft_fdc_rate_limit=4000

# modprobe parport || true # pre-2.1 kernels don't have the parport module
# insmod ./trakker.o
# insmod ./bpck-fdc.o
#
# maybe your klogd doesn't understand `-i', refer to its man-page ...
#
#/usr/sbin/klogd -i

