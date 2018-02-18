IntelGraphicsFixup
==================

An open source kernel extension providing patches to select Intel GPUs.

#### Features
- Fixes PAVP freezes on Intel Capri Graphics (HD4000), Azul Graphics (HD4400, HD4600) and Intel Skylake/Kabylake Graphics
- Fixes boot logo on all known Intel Graphics starting with HD4000 (via frame buffer reset or via restoring video memory content)
- Display initialization fix for Intel Skylake Graphics 

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu), PAVP reversing and AppleIntelFramebuffer reversing & extended boot logo fix
- [YungRaj](https://github.com/YungRaj) for fix for Sky Lake
- [lvs1974](https://applelife.ru/members/lvs1974.53809/) for writing the software and maintaining it
- [Vandroiy](https://github.com/vandroiy2013) for testing & frame buffer fix for Kaby Lake and Azul
- [PMheart](https://github.com/PMheart) for writing the fix for HD4000
- [syscl](https://github.com/syscl) for writing the fix for HD3000
