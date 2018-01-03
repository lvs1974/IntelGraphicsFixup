//
//  kern_igfx.cpp
//  IGFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/kern_api.hpp>
#include <Library/LegacyIOService.h>
#include <IOKit/IOPlatformExpert.h>
#define protected public
#include <IOKit/graphics/IOFramebuffer.h>
#undef protected

#include "kern_igfx.hpp"

// Only used in apple-driven callbacks
static IGFX *callbackIgfx = nullptr;
static KernelPatcher *callbackPatcher = nullptr;

static const char *kextHD4000Path[]          { "/System/Library/Extensions/AppleIntelHD4000Graphics.kext/Contents/MacOS/AppleIntelHD4000Graphics" };
static const char *kextHD5000Path[]          { "/System/Library/Extensions/AppleIntelHD5000Graphics.kext/Contents/MacOS/AppleIntelHD5000Graphics" };
static const char *kextSKLPath[]             { "/System/Library/Extensions/AppleIntelSKLGraphics.kext/Contents/MacOS/AppleIntelSKLGraphics" };
static const char *kextSKLFramebufferPath[]  { "/System/Library/Extensions/AppleIntelSKLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelSKLGraphicsFramebuffer" };
static const char *kextKBLPath[]             { "/System/Library/Extensions/AppleIntelKBLGraphics.kext/Contents/MacOS/AppleIntelKBLGraphics" };
static const char *kextKBLFramebufferPath[]  { "/System/Library/Extensions/AppleIntelKBLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelKBLGraphicsFramebuffer" };
static const char *kextIOGraphicsPath[]      { "/System/Library/Extensions/IOGraphicsFamily.kext/IOGraphicsFamily" };

static KernelPatcher::KextInfo kextList[] {
    { "com.apple.driver.AppleIntelHD4000Graphics",         kextHD4000Path,         1, {}, {}, KernelPatcher::KextInfo::Unloaded },
    { "com.apple.driver.AppleIntelHD5000Graphics",         kextHD5000Path,         1, {}, {}, KernelPatcher::KextInfo::Unloaded },
    { "com.apple.driver.AppleIntelSKLGraphics",            kextSKLPath,            1, {}, {}, KernelPatcher::KextInfo::Unloaded },
    { "com.apple.driver.AppleIntelSKLGraphicsFramebuffer", kextSKLFramebufferPath, 1, {}, {}, KernelPatcher::KextInfo::Unloaded },
    { "com.apple.driver.AppleIntelKBLGraphics",            kextKBLPath,            1, {}, {}, KernelPatcher::KextInfo::Unloaded },
    { "com.apple.driver.AppleIntelKBLGraphicsFramebuffer", kextKBLFramebufferPath, 1, {}, {}, KernelPatcher::KextInfo::Unloaded },
    { "com.apple.iokit.IOGraphicsFamily",                  kextIOGraphicsPath,     1, {true}, {}, KernelPatcher::KextInfo::Unloaded },
};

static size_t kextListSize = arrsize(kextList);

bool IGFX::init() {
	PE_parse_boot_argn("igfxrst", &resetFramebuffer, sizeof(resetFramebuffer));
    
    char tmp[20];
    if (!PE_parse_boot_argn("-igfxvesa", tmp, sizeof(tmp)))
        progressState |= ProcessingState::CallbackVesaMode;

	// We need to load vinfo in all cases but reset
	if (resetFramebuffer != FBRESET) {
		auto error = lilu.onPatcherLoad([](void *user, KernelPatcher &patcher) {
			static_cast<IGFX *>(user)->processKernel(patcher);
		}, this);

		if (error != LiluAPI::Error::NoError) {
			SYSLOG("igfx", "failed to register onPatcherLoad method %d", error);
			return false;
		}
	}

	auto error = lilu.onKextLoad(kextList, kextListSize, [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
		callbackIgfx = static_cast<IGFX *>(user);
		callbackPatcher = &patcher;
		callbackIgfx->processKext(patcher, index, address, size);
	}, this);
	
	if (error != LiluAPI::Error::NoError) {
		SYSLOG("igfx", "failed to register onKextLoad method %d", error);
		return false;
	}
	
	return true;
}

void IGFX::deinit() {}

uint32_t IGFX::pavpSessionCallback(void *intelAccelerator, PAVPSessionCommandID_t passed_session_cmd, uint32_t a3, uint32_t *a4, bool passed_flag) {
	//DBGLOG("igfx, "pavpCallback: passed_session_cmd = %d, passed_flag = %d, a3 = %d, a4 = %s", passed_session_cmd, passed_flag, a3, a4 == nullptr ? "null" : "not null");
	
	if (callbackIgfx && callbackPatcher && callbackIgfx->orgPavpSessionCallback) {
		if (passed_session_cmd == 4) {
			DBGLOG("igfx", "pavpSessionCallback: enforcing error on cmd 4 (send to ring?)!");
			return 0xE00002D6; // or 0
		}
		
		return callbackIgfx->orgPavpSessionCallback(intelAccelerator, passed_session_cmd, a3, a4, passed_flag);
	}
	
	SYSLOG("igfx", "callback arrived at nowhere");
	return 0;
}

void IGFX::frameBufferInit(void *that) {
	if (callbackIgfx && callbackPatcher && callbackIgfx->gIOFBVerboseBootPtr && callbackIgfx->orgFrameBufferInit) {
		bool tryBackCopy = callbackIgfx->gotInfo && callbackIgfx->resetFramebuffer != FBRESET;
		auto &info = callbackIgfx->vinfo;

		// Copy back usually happens in a separate call to frameBufferInit
		// Furthermore, v_baseaddr may not be available on subsequent calls, so we have to copy
		if (tryBackCopy && info.v_baseaddr) {
			callbackIgfx->consoleBuffer = Buffer::create<uint8_t>(info.v_rowbytes * info.v_height);
            if (callbackIgfx->consoleBuffer)
				lilu_os_memcpy(callbackIgfx->consoleBuffer, reinterpret_cast<uint8_t *>(info.v_baseaddr), info.v_rowbytes * info.v_height);
            else
				SYSLOG("igfx", "console buffer allocation failure");
			// Even if we may succeed next time, it will be unreasonably dangerous
			info.v_baseaddr = 0;
		}

		uint8_t verboseBoot = *callbackIgfx->gIOFBVerboseBootPtr;
		// For back copy we need a console buffer and no verbose
		tryBackCopy = tryBackCopy && callbackIgfx->consoleBuffer && !verboseBoot;

		// Now check if the resolution and parameters match
		auto fb = static_cast<IOFramebuffer *>(that);
		if (tryBackCopy) {
			IODisplayModeID mode;
			IOIndex depth;
			IOPixelInformation pixelInfo;

			if (fb->getCurrentDisplayMode(&mode, &depth) == kIOReturnSuccess &&
				fb->getPixelInformation(mode, depth, kIOFBSystemAperture, &pixelInfo) == kIOReturnSuccess) {
				DBGLOG("igfx", "fb info 1: %d:%d %d:%d:%d",
					   mode, depth, pixelInfo.bytesPerRow, pixelInfo.bytesPerPlane, pixelInfo.bitsPerPixel);
				DBGLOG("igfx", "fb info 2: %d:%d %s %d:%d:%d",
					   pixelInfo.componentCount, pixelInfo.bitsPerComponent, pixelInfo.pixelFormat, pixelInfo.flags, pixelInfo.activeWidth, pixelInfo.activeHeight);

				if (info.v_rowbytes != pixelInfo.bytesPerRow || info.v_width != pixelInfo.activeWidth ||
					info.v_height != pixelInfo.activeHeight || info.v_depth != pixelInfo.bitsPerPixel) {
					tryBackCopy = false;
					DBGLOG("igfx", "this display has different mode");
				}
			} else {
				DBGLOG("igfx", "failed to obtain display mode");
				tryBackCopy = false;
			}
		}

		if (!tryBackCopy) *callbackIgfx->gIOFBVerboseBootPtr = 1;
		callbackIgfx->orgFrameBufferInit(that);
		if (!tryBackCopy) *callbackIgfx->gIOFBVerboseBootPtr = verboseBoot;

		if (tryBackCopy && fb->fVramMap) {
			auto src = reinterpret_cast<uint8_t *>(callbackIgfx->consoleBuffer);
			auto dst = reinterpret_cast<uint8_t *>(fb->fVramMap->getVirtualAddress());

			DBGLOG("igfx", "attempting to copy...");
			// Here you can actually draw at your will, but looks like only on Intel.
			lilu_os_memcpy(dst, src, info.v_rowbytes * info.v_height);
		}
	}
}

bool IGFX::computeLaneCount(void *that, void *unk1, unsigned int bpp, int unk3, int *lane_count) {
	// unk3 is read in AppleIntelFramebufferController::GetDPCDInfo
	DBGLOG("igfx", "computeLaneCount: bpp = %d, unk3 = %d", bpp, unk3); // 24 0
	
	// HD 530 reports to have 0 lanes max
	bool r = callbackIgfx->orgComputeLaneCount(that, unk1, bpp, unk3, lane_count);
	if (!r && *lane_count == 0) {
		DBGLOG("igfx", "reporting worked lane count");
		r = true;
	}
	
	return r;
}

bool IGFX::intelGraphicsStart(IOService *that, IOService *provider)
{
    DBGLOG("Intel Graphics", "Prevent starting controller");
    return false;
}

void IGFX::processKernel(KernelPatcher &patcher) {
	auto info = reinterpret_cast<vc_info *>(patcher.solveSymbol(KernelPatcher::KernelID, "_vinfo"));
	if (info) {
		vinfo = *info;
		DBGLOG("igfx", "vinfo 1: %d:%d %d:%d:%d",
			   vinfo.v_height, vinfo.v_width, vinfo.v_depth, vinfo.v_rowbytes, vinfo.v_type);
		DBGLOG("igfx", "vinfo 2: %s %d:%d %d:%d:%d",
			   vinfo.v_name, vinfo.v_rows, vinfo.v_columns, vinfo.v_rowscanbytes, vinfo.v_scale, vinfo.v_rotate);
		gotInfo = true;
	} else {
		SYSLOG("igfx", "failed to obtain vcinfo");
	}
    
    // Ignore all the errors for other processors
    patcher.clearError();
}

void IGFX::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	if (progressState != ProcessingState::EverythingDone) {
		for (size_t i = 0; i < kextListSize; i++) {
			if (kextList[i].loadIndex == index) {
				DBGLOG("igfx", "found %s (%d)", kextList[i].id, progressState);

				if (!(progressState & ProcessingState::CallbackPavpSessionRouted) &&
					(!strcmp(kextList[i].id, "com.apple.driver.AppleIntelHD5000Graphics") ||
					 !strcmp(kextList[i].id, "com.apple.driver.AppleIntelSKLGraphics") ||
					 !strcmp(kextList[i].id, "com.apple.driver.AppleIntelKBLGraphics"))) {
					auto sessionCallback = patcher.solveSymbol(index, "__ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
					if (sessionCallback) {
						DBGLOG("igfx", "obtained __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
						patcher.clearError();
						orgPavpSessionCallback = reinterpret_cast<t_pavp_session_callback>(patcher.routeFunction(sessionCallback, reinterpret_cast<mach_vm_address_t>(pavpSessionCallback), true));
						if (patcher.getError() == KernelPatcher::Error::NoError) {
							DBGLOG("igfx", "routed __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
							progressState |= ProcessingState::CallbackPavpSessionRouted;
						} else {
							SYSLOG("igfx", "failed to route __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
						}
					} else {
						SYSLOG("igfx", "failed to resolve __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
					}
				}
				
				if (!(progressState & ProcessingState::CallbackPavpSessionHD4000Routed) &&
					(!strcmp(kextList[i].id, "com.apple.driver.AppleIntelHD4000Graphics"))) {
					auto sessionCallbackHD4000 = patcher.solveSymbol(index, "__ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
					if (sessionCallbackHD4000) {
						DBGLOG("igfx", "obtained __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
						patcher.clearError();
						orgPavpSessionCallback = reinterpret_cast<t_pavp_session_callback>(patcher.routeFunction(sessionCallbackHD4000, reinterpret_cast<mach_vm_address_t>(pavpSessionCallback), true));
						if (patcher.getError() == KernelPatcher::Error::NoError) {
							DBGLOG("igfx", "routed __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
							progressState |= ProcessingState::CallbackPavpSessionHD4000Routed;
						} else {
							SYSLOG("igfx", "failed to route __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
						}
					} else {
						SYSLOG("igfx", "failed to resolve __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
					}
				}
				
				
				if (!(progressState & ProcessingState::CallbackFrameBufferInitRouted) && !strcmp(kextList[i].id, "com.apple.iokit.IOGraphicsFamily")) {
					if (getKernelVersion() >= KernelVersion::Yosemite) {
						gIOFBVerboseBootPtr = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZL16gIOFBVerboseBoot"));
						if (gIOFBVerboseBootPtr) {
							DBGLOG("igfx", "obtained __ZL16gIOFBVerboseBoot");
							auto ioFramebufferinit = patcher.solveSymbol(index, "__ZN13IOFramebuffer6initFBEv");
							if (ioFramebufferinit) {
								DBGLOG("igfx", "obtained __ZN13IOFramebuffer6initFBEv");
								patcher.clearError();
								orgFrameBufferInit = reinterpret_cast<t_frame_buffer_init>(patcher.routeFunction(ioFramebufferinit, reinterpret_cast<mach_vm_address_t>(frameBufferInit), true));
								if (patcher.getError() == KernelPatcher::Error::NoError) {
									DBGLOG("igfx", "routed __ZN13IOFramebuffer6initFBEv");
									progressState |= ProcessingState::CallbackFrameBufferInitRouted;
								} else {
									SYSLOG("igfx", "failed to route __ZN13IOFramebuffer6initFBEv");
								}
							}
						} else {
							SYSLOG("igfx", "failed to resolve __ZL16gIOFBVerboseBoot");
						}
					} else {
						progressState |= ProcessingState::CallbackFrameBufferInitRouted;
					}
				}
				
				if (!(progressState & ProcessingState::CallbackComputeLaneCountRouted) &&
					(!strcmp(kextList[i].id, "com.apple.driver.AppleIntelSKLGraphicsFramebuffer") ||
					 !strcmp(kextList[i].id, "com.apple.driver.AppleIntelKBLGraphicsFramebuffer"))) {
					DBGLOG("igfx", "found %s", kextList[i].id);
					auto compute_lane_count = patcher.solveSymbol(index, "__ZN31AppleIntelFramebufferController16ComputeLaneCountEPK29IODetailedTimingInformationV2jjPj");
					if (compute_lane_count) {
						DBGLOG("igfx", "obtained ComputeLaneCount");
						patcher.clearError();
						orgComputeLaneCount = reinterpret_cast<t_compute_lane_count>(patcher.routeFunction(compute_lane_count, reinterpret_cast<mach_vm_address_t>(computeLaneCount), true));
						if (patcher.getError() == KernelPatcher::Error::NoError) {
							DBGLOG("igfx", "routed ComputeLaneCount");
							progressState |= ProcessingState::CallbackComputeLaneCountRouted;
						} else {
							SYSLOG("igfx", "failed to route ComputeLaneCount");
						}
					} else {
						SYSLOG("igfx", "failed to resolve ComputeLaneCount");
					}
				}
                
                if (!(progressState & ProcessingState::CallbackVesaMode) &&
                    (!strcmp(kextList[i].id, "com.apple.driver.AppleIntelHD4000Graphics") ||
                     !strcmp(kextList[i].id, "com.apple.driver.AppleIntelHD5000Graphics") ||
                     !strcmp(kextList[i].id, "com.apple.driver.AppleIntelSKLGraphics") ||
                     !strcmp(kextList[i].id, "com.apple.driver.AppleIntelKBLGraphics"))) {
                
                    auto acceleratorStart = patcher.solveSymbol(index, "__ZN16IntelAccelerator5startEP9IOService");
                    if (acceleratorStart) {
                        DBGLOG("igfx", "obtained IntelAccelerato::start");
                        patcher.clearError();
                        patcher.routeFunction(acceleratorStart, reinterpret_cast<mach_vm_address_t>(intelGraphicsStart), true);
                        if (patcher.getError() == KernelPatcher::Error::NoError) {
                            DBGLOG("igfx", "routed IntelAccelerator::start");
                            progressState |= ProcessingState::CallbackVesaMode;
                        } else {
                            SYSLOG("igfx", "failed to route IntelAccelerator::start");
                        }
                    } else {
                        SYSLOG("igfx", "failed to resolve IntelAccelerator::start");
                    }
                }
			}
		}
	}
	
	// Ignore all the errors for other processors
	patcher.clearError();
}
