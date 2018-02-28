IntelGraphicsFixup Changelog
============================
#### v1.0.0
- Initial release

#### v1.0.1
- Updated readme, features
- Patch IOGraphicsFamily if it's already loaded

#### v1.1.0
- Adopted for Lulu 1.1.0
- Module identifier has been fixed

#### v1.1.1
- Sky Lake support

#### v1.1.2
- Display initialization fix for SkyLake Graphics

#### v1.1.3
- Added OSBundleCompatibleVersion

#### v1.1.4
- Fix for Kaby Lake Graphics added (Display initialization)

#### v1.1.5
- Fix for Azul Graphics added (Display initialization)

#### v1.1.6
- HighSierra compatibility, and bug fix: clear error status before routing

#### v1.1.7
- Fix for HD4000 added (credits to PMHeart)

#### v1.2.0
- XCode 9 & Lilu 1.2.0 compatibility fixes (Lilu 1.2.0 is required)

#### v1.2.1
- New boot  logo fix via restoring video memory content (no frame buffer reset)

#### v1.2.2
- New boot-arg -igfxvesa is supported (to disable intel video acceleration completely)

#### v1.2.3
- Fix up will be loaded in safe mode (required to fix black screen)

#### v1.2.4
- Fix for HD3000 added (credits to syscl)

#### v1.2.5
- Add basic automatic IGPU model detection if it is not set
- Add IGPU device id correction (remove all the other kexts, non-default values may be passed via `igfxfake=val`)
- Add basic digital audio correction on (HDAU rename on Haswell and `layout-id`/`hda-gfx` where needed)
- Add GFX0 -> IGPU automatic rename
- Add MEI/HECI -> IMEI automatic rename
- Add IMEI device id automatic correction on Sandy Bridge and Ivy Bridge
- Add basic automatic AAPL,ig-platform-id injection (defaults for SKL/KBL and connector-less when AMD/NVIDIA is found)
- Add GuC microcode loading on SKL (9.33) and KBL (9.39) in 10.13 (enabled via `igfxfw=1` boot-arg)
- Add `igfxframe=frame` boot-arg for framebuffer id injection in testing cases
- Fix booting without `-disablegfxfirmware` boot argument on KBL GPUs
- Fix compatibility with connector-less frames
- Minor performance improvements
