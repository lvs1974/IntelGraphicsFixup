//
//  kern_azul_pavp_disabler.cpp
//  AzulPAVPDisabler_impl
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/kern_api.hpp>
#include <Library/LegacyIOService.h>

#include <mach/vm_map.h>
#include <IOKit/IORegistryEntry.h>

#include "kern_igfx.hpp"

// Only used in apple-driven callbacks
static IGFX *callbackIgfx = nullptr;
static KernelPatcher *callbackPatcher = nullptr;

static const char *kextHD5000Path[]         { "/System/Library/Extensions/AppleIntelHD5000Graphics.kext/Contents/MacOS/AppleIntelHD5000Graphics" };
static const char *kextSKLPath[]            { "/System/Library/Extensions/AppleIntelSKLGraphics.kext/Contents/MacOS/AppleIntelSKLGraphics" };
static const char *kextSKLFramebufferPath[] { "/System/Library/Extensions/AppleIntelSKLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelSKLGraphicsFramebuffer" };
static const char *kextIOGraphicsPath[]     { "/System/Library/Extensions/IOGraphicsFamily.kext/IOGraphicsFamily" };

static KernelPatcher::KextInfo kextList[] {
	{ "com.apple.driver.AppleIntelHD5000Graphics", kextHD5000Path, 1, false, {}, KernelPatcher::KextInfo::Unloaded },
    { "com.apple.driver.AppleIntelSKLGraphics", kextSKLPath, 1, false, {}, KernelPatcher::KextInfo::Unloaded },
    { "com.apple.driver.AppleIntelSKLGraphicsFramebuffer", kextSKLFramebufferPath, 1, true, {}, KernelPatcher::KextInfo::Unloaded },
    { "com.apple.iokit.IOGraphicsFamily", kextIOGraphicsPath, 1, true, {}, KernelPatcher::KextInfo::Unloaded },
};

static size_t kextListSize {4};

bool IGFX::init() {
    LiluAPI::Error error = lilu.onKextLoad(kextList, kextListSize,
	[](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
		callbackIgfx = static_cast<IGFX *>(user);
		callbackPatcher = &patcher;
		callbackIgfx->processKext(patcher, index, address, size);
	}, this);
	
	if (error != LiluAPI::Error::NoError) {
		SYSLOG("igfx @ failed to register onPatcherLoad method %d", error);
		return false;
	}
	
	return true;
}

void IGFX::deinit() {
}

uint32_t IGFX::pavpSessionCallback(void *intelAccelerator, PAVPSessionCommandID_t passed_session_cmd, uint32_t a3, uint32_t *a4, bool passed_flag) {
    //DBGLOG("igfx @ pavpCallback: passed_session_cmd = %d, passed_flag = %d, a3 = %d, a4 = %s", passed_session_cmd, passed_flag, a3, a4 == nullptr ? "null" : "not null");
    
    if (callbackIgfx && callbackPatcher && callbackIgfx->orgPavpSessionCallback) {
        if (passed_session_cmd == 4) {
            DBGLOG("igfx @ pavpSessionCallback: enforcing error on cmd 4 (send to ring?)!");
            return 0xE00002D6; // or 0
        }
        
        return callbackIgfx->orgPavpSessionCallback(intelAccelerator, passed_session_cmd, a3, a4, passed_flag);
    }
	
	SYSLOG("igfx @ callback arrived at nowhere");
    return 0;
}

void IGFX::frameBufferInit(void *that) {
    if (callbackIgfx && callbackPatcher && callbackIgfx->gIOFBVerboseBootPtr && callbackIgfx->orgFrameBufferInit) {
        uint8_t verboseBoot = *callbackIgfx->gIOFBVerboseBootPtr;
        *callbackIgfx->gIOFBVerboseBootPtr = 1;
        callbackIgfx->orgFrameBufferInit(that);
        *callbackIgfx->gIOFBVerboseBootPtr = verboseBoot;
    }
}

bool IGFX::computeLaneCount(void *framebuffer, void *unk1, unsigned int unk2, int unk3, int *lane_count) {
    DBGLOG("igfx @ computeLaneCount: unk2 = %d, unk3 = %d", unk2, unk3);
    
    if (!callbackIgfx->orgComputeLaneCount(framebuffer, unk1, unk2, unk3, lane_count)) {
        SYSLOG("igfx @ computeLaneCount fixing lane_count %d", *lane_count);
        *lane_count = 1;
    }
    
    return true;
}

void IGFX::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	if (progressState != ProcessingState::EverythingDone) {
		for (size_t i = 0; i < kextListSize; i++) {
			if (kextList[i].loadIndex == index) {
                if (!(progressState & ProcessingState::CallbackPavpSessionRouted) && (!strcmp(kextList[i].id, "com.apple.driver.AppleIntelHD5000Graphics") ||
                	!strcmp(kextList[i].id, "com.apple.driver.AppleIntelSKLGraphics")))
                {
                    DBGLOG("igfx @ found %s", kextList[i].id);
                    auto sessionCallback = patcher.solveSymbol(index, "__ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
                    if (sessionCallback) {
                        DBGLOG("igfx @ obtained __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
                        orgPavpSessionCallback = reinterpret_cast<t_pavp_session_callback>(patcher.routeFunction(sessionCallback, reinterpret_cast<mach_vm_address_t>(pavpSessionCallback), true));
                        if (patcher.getError() == KernelPatcher::Error::NoError) {
                            DBGLOG("igfx @ routed __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
                            progressState |= ProcessingState::CallbackPavpSessionRouted;
                        } else {
                            SYSLOG("igfx @ failed to route __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
                        }
                    } else {
                        SYSLOG("igfx @ failed to resolve __ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
                    }
                }
                
                if (!(progressState & ProcessingState::CallbackFrameBufferInitRouted) && !strcmp(kextList[i].id, "com.apple.iokit.IOGraphicsFamily"))
                {
                    DBGLOG("igfx @ found com.apple.iokit.IOGraphicsFamily");
                    gIOFBVerboseBootPtr = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZL16gIOFBVerboseBoot"));
                    if (gIOFBVerboseBootPtr) {
                        DBGLOG("igfx @ obtained __ZL16gIOFBVerboseBoot");
                        auto ioFramebufferinit = patcher.solveSymbol(index, "__ZN13IOFramebuffer6initFBEv");
                        if (ioFramebufferinit) {
                            DBGLOG("igfx @ obtained __ZN13IOFramebuffer6initFBEv");
                            orgFrameBufferInit = reinterpret_cast<t_frame_buffer_init_callback>(patcher.routeFunction(ioFramebufferinit, reinterpret_cast<mach_vm_address_t>(frameBufferInit), true));
                            if (patcher.getError() == KernelPatcher::Error::NoError) {
                                DBGLOG("igfx @ routed __ZN13IOFramebuffer6initFBEv");
                                progressState |= ProcessingState::CallbackFrameBufferInitRouted;
                            } else {
                                SYSLOG("igfx @ failed to route __ZN13IOFramebuffer6initFBEv");
                            }
                        }
                    } else {
                        SYSLOG("igfx @ failed to resolve __ZL16gIOFBVerboseBoot");
                    }
				}
                
                if (!(progressState & ProcessingState::CallbackSKLComputeLaneCountRouted) && !strcmp(kextList[i].id, "com.apple.driver.AppleIntelSKLGraphicsFramebuffer"))
                {
                    DBGLOG("igfx @ found com.apple.driver.AppleIntelSKLGraphicsFramebuffer");
                    auto compute_lane_count = patcher.solveSymbol(index, "__ZN31AppleIntelFramebufferController16ComputeLaneCountEPK29IODetailedTimingInformationV2jjPj");
                    if (compute_lane_count) {
                        DBGLOG("igfx @ obtained __ZN31AppleIntelFramebufferController16ComputeLaneCountEPK29IODetailedTimingInformationV2jjPj");
                        orgComputeLaneCount = reinterpret_cast<t_compute_lane_count_callback>(patcher.routeFunction(compute_lane_count, reinterpret_cast<mach_vm_address_t>(computeLaneCount), true));
                        if (patcher.getError() == KernelPatcher::Error::NoError) {
                            DBGLOG("igfx @ routed __ZN31AppleIntelFramebufferController16ComputeLaneCountEPK29IODetailedTimingInformationV2jjPj");
                            progressState |= ProcessingState::CallbackSKLComputeLaneCountRouted;
                        } else {
                            SYSLOG("igfx @ failed to route __ZN31AppleIntelFramebufferController16ComputeLaneCountEPK29IODetailedTimingInformationV2jjPj");
                        }
                    } else {
                        SYSLOG("igfx @ failed to resolve __ZN31AppleIntelFramebufferController16ComputeLaneCountEPK29IODetailedTimingInformationV2jjPj");
                    }
                }
			}
		}
	}
	
	// Ignore all the errors for other processors
	patcher.clearError();
}

