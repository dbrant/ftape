# ftape Driver Modernization

This repository contains the modernized QIC-117 floppy tape driver for Linux, updated to work with modern kernel versions.

## What Was Updated

### 1. Module System Modernization
- Replaced `init_module()` / `cleanup_module()` with `module_init()` / `module_exit()`
- Updated `MODULE_PARM` to modern `module_param()` API
- Removed deprecated `register_symtab()` calls

### 2. Header Files
- Removed deprecated `linux/config.h`, `linux/wrapper.h`, `asm/system.h` 
- Fixed include paths for modern kernel compatibility
- Updated version checking macros

### 3. Memory Management  
- Replaced deprecated `virt_to_bus()` with `virt_to_phys()`
- Updated DMA buffer allocation methods
- Fixed memory management for modern kernel APIs

### 4. I/O Port Handling
- Replaced deprecated `check_region()` with proper `request_region()`
- Added proper resource cleanup on failure
- Modernized port allocation/deallocation

### 5. Module Reference Counting
- Replaced `MOD_INC_USE_COUNT` / `MOD_DEC_USE_COUNT` with `try_module_get()` / `module_put()`

### 6. Build System
- Created modern Kconfig for kernel configuration
- Updated Makefile for out-of-tree compilation
- Supports modern kernel build system (Kbuild)

## Building Out-of-Tree

You can now compile this driver as an out-of-tree module without needing to copy it into the kernel source tree:

```bash
# Build all modules
make

# Clean build files
make clean

# Install modules (requires root)
sudo make install
```

## Available Modules

- **ftape-core.ko** - Core ftape driver
- **ftape-internal.ko** - Internal FDC support  
- **zftape.ko** - VFS interface for ftape

## Compatibility

This modernized driver is compatible with:
- Linux kernel 4.x and later
- Linux kernel 5.x series  
- Linux kernel 6.x series (tested with 6.8.x)

## Hardware Support

Supports the same hardware as the original driver:
- QIC-40/80/3010/3020 floppy tape drives
- Internal floppy controller connections
- Parallel port tape drives (Trakker, BackPack)
- Various FDC types (8272, 82077, 82078, etc.)

## Installation

1. Build the modules: `make`
2. Install modules: `sudo make install`  
3. Load the driver: `sudo modprobe ftape-core`
4. Create device nodes: `sudo ./scripts/MAKEDEV.ftape`

## Notes

- Some warning messages during compilation are normal and don't affect functionality
- The driver maintains compatibility with the original ftape interface
- Module parameters can be set during loading (see original documentation)

## Original Credits

This modernization preserves all original copyright notices and credits:
- Copyright (C) 1993-1996 Bas Laarhoven
- Copyright (C) 1995-1996 Kai Harrekilde-Petersen  
- Copyright (C) 1996-2000 Claus-Justus Heine

Modernization updates by ftape maintainer for modern kernel compatibility.