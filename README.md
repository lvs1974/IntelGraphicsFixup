IntelGraphicsFixup
==================

An open source kernel extension providing patches to select Intel GPUs.

#### Features
- Fixes PAVP freezes with Intel Capri Graphics (HD4000), Azul Graphics (HD4400, HD4600) and Intel Skylake/Kaby Lake Graphics
- Fixes boot logo with all known Intel Graphics starting with HD4000 (via frame buffer reset or via restoring video memory content)
- Fixes display initialization with Intel Skylake and Kaby Lake Graphics
- Performs necessary IGPU, digital audio, and IMEI property correction and renaming
- Ensures a correct IGPU model in I/O Registry (when it is not specified manually)
- Supports IGPU device-id faking (correct `device-id` should be set via device properties or ACPI)
- Allows booting without  `-disablegfxfirmware`  boot argument with Kaby Lake Graphics in 10.13
- Allows booting in VESA mode with Intel HD graphics (via `-igfxvesa` boot argument)
- Performs basic framebuffer id injection when none is specified (connector-less frames will be used with discrete GPUs)
- Allows GuC microcode loading with Intel Skylake/Kaby Lake Graphics in 10.13 (via `igfxfw=1` boot argument)

#### Configuration
Add `-igfxdbg` to enable debug printing (available in DEBUG binaries).  
Add `-igfxoff` to disable IntelGraphicsFixup.  
Add `-igfxbeta` to enable IntelGraphicsFixup on unsupported os versions (10.13 and below are enabled by default).  
Add `-igfxvesa` to boot Intel graphics without hardware acceleration (VESA mode).  
Add `igfxfw=1` to enable GuC microcode loading in 10.13 or newer.  
Add `igfxrst=1` to prefer drawing Apple logo at 2nd boot stage instead of framebuffer copying.  
Add `igfxframe=frame` to inject a dedicated framebuffer identifier into IGPU (only for TESTING purposes).  

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu), PAVP reversing and AppleIntelFramebuffer reversing & extended boot logo fix
- [YungRaj](https://github.com/YungRaj) for fix for Sky Lake
- [lvs1974](https://applelife.ru/members/lvs1974.53809/) for writing the software and maintaining it
- [Vandroiy](https://github.com/vandroiy2013) for testing & frame buffer fix for Kaby Lake and Azul
- [PMheart](https://github.com/PMheart) for writing the fix for HD4000
- [syscl](https://github.com/syscl) for writing the fix for HD3000, adding support for HD P630, UHD620, HD610
