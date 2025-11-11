#!/bin/bash
#
# Example module insertion script for ftape-4.x
#
# Please modify to reflect your hardware configuration
#

# first make sure to remove the "floppy" kernel module to free up the resources it's using
rmmod floppy

# isapnp ./isapnp.conf
insmod ./ftape-core.ko ft_soft_retries=1 ft_ignore_ecc_err=1 ft_tracings=3,3,3,3,3 # ft_fdc_driver=ftape-internal,bpck-fdc:trakker,none,none ft_tracings=3,3,3,3,3
insmod ./zftape.ko # ft_major_device_number=27 # ${27-FT_MAJOR}

# Don't use the rate limit parameter if everything works without it.
# Valid setting for "rate_limit" are 4000, 3000, 2000, 1000 and 500
# (500 is pretty useless).
#
# Ditto Max drives needs at least 2000Kbps.
# TR-3 tape drives need at least 1000Kbps.
# QIC-80 tape drives need at least 500Kbps.
# QIC-40 tape drives work at 250Kbps.
#

insmod ./ftape-internal.ko ft_fdc_rate_limit=1000 # ft_fdc_fc10=0 ft_fdc_mach2=0 ft_fdc_base=0x210 ft_fdc_dma=0 ft_fdc_threshold=15
