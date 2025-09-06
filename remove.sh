#!/bin/bash
#
# remove all ftape modules 
#
rmmod ftape_bpck || true
rmmod ftape_trakker || true
rmmod ftape_parport || true
rmmod ftape_internal || true
rmmod zft_compressor || true
rmmod zftape || true
rmmod ftape_core || true
#rmmod parport_probe || true
#rmmod parport_pc || true
#rmmod ppdev || true
#rmmod parport || true

