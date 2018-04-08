//
//  kern_igfx.cpp
//  IGFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_cpu.hpp>
#include <Headers/kern_iokit.hpp>
#include <Library/LegacyIOService.h>
#include <IOKit/IOPlatformExpert.h>
#define protected public
#include <IOKit/graphics/IOFramebuffer.h>
#undef protected

#include "kern_igfx.hpp"
#include "kern_audio.hpp"
#include "kern_guc.hpp"
#include "kern_model.hpp"
#include "kern_regs.hpp"

// Only used in apple-driven callbacks
static IGFX *callbackIgfx = nullptr;
static KernelPatcher *callbackPatcher = nullptr;

static const char *kextHD3000Path[]          { "/System/Library/Extensions/AppleIntelHD3000Graphics.kext/Contents/MacOS/AppleIntelHD3000Graphics" };
static const char *kextHD4000Path[]          { "/System/Library/Extensions/AppleIntelHD4000Graphics.kext/Contents/MacOS/AppleIntelHD4000Graphics" };
static const char *kextHD5000Path[]          { "/System/Library/Extensions/AppleIntelHD5000Graphics.kext/Contents/MacOS/AppleIntelHD5000Graphics" };
static const char *kextBDWPath[]             { "/System/Library/Extensions/AppleIntelBDWGraphics.kext/Contents/MacOS/AppleIntelBDWGraphics" };
static const char *kextSKLPath[]             { "/System/Library/Extensions/AppleIntelSKLGraphics.kext/Contents/MacOS/AppleIntelSKLGraphics" };
static const char *kextSKLFramebufferPath[]  { "/System/Library/Extensions/AppleIntelSKLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelSKLGraphicsFramebuffer" };
static const char *kextKBLPath[]             { "/System/Library/Extensions/AppleIntelKBLGraphics.kext/Contents/MacOS/AppleIntelKBLGraphics" };
static const char *kextKBLFramebufferPath[]  { "/System/Library/Extensions/AppleIntelKBLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelKBLGraphicsFramebuffer" };
static const char *kextIOGraphicsPath[]      { "/System/Library/Extensions/IOGraphicsFamily.kext/IOGraphicsFamily" };

static KernelPatcher::KextInfo kextList[] {
	{ "com.apple.driver.AppleIntelHD3000Graphics",         kextHD3000Path,         0, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelHD4000Graphics",         kextHD4000Path,         0, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelHD5000Graphics",         kextHD5000Path,         0, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelBDWGraphics",            kextBDWPath,            0, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelSKLGraphics",            kextSKLPath,            0, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelSKLGraphicsFramebuffer", kextSKLFramebufferPath, 0, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelKBLGraphics",            kextKBLPath,            0, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelKBLGraphicsFramebuffer", kextKBLFramebufferPath, 0, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.iokit.IOGraphicsFamily",                  kextIOGraphicsPath,     0, {true}, {}, KernelPatcher::KextInfo::Unloaded },
};

enum : size_t {
	KextHD3000Graphics,
	KextHD4000Graphics,
	KextHD5000Graphics,
	KextBDWGraphics,
	KextSKLGraphics,
	KextSKLGraphicsFramebuffer,
	KextKBLGraphics,
	KextKBLGraphicsFramebuffer,
	KextIOGraphicsFamily
};

static size_t kextListSize = arrsize(kextList);

bool IGFX::init() {
	access = IOLockAlloc();

	PE_parse_boot_argn("igfxrst", &resetFramebuffer, sizeof(resetFramebuffer));
	PE_parse_boot_argn("igfxfw", &decideLoadScheduler, sizeof(decideLoadScheduler));

	uint32_t family = 0, model = 0;
	cpuGeneration = CPUInfo::getGeneration(&family, &model);
	switch (cpuGeneration) {
		case CPUInfo::CpuGeneration::SandyBridge:
			kextList[KextHD3000Graphics].pathNum = arrsize(kextHD3000Path);
			progressState |= ProcessingState::CallbackComputeLaneCountRouted |
				ProcessingState::CallbackGuCFirmwareUpdateRouted;
			break;
		case CPUInfo::CpuGeneration::IvyBridge:
			kextList[KextHD4000Graphics].pathNum = arrsize(kextHD4000Path);
			progressState |= ProcessingState::CallbackComputeLaneCountRouted |
				ProcessingState::CallbackGuCFirmwareUpdateRouted;
			break;
		case CPUInfo::CpuGeneration::Haswell:
			kextList[KextHD5000Graphics].pathNum = arrsize(kextHD5000Path);
			progressState |= ProcessingState::CallbackComputeLaneCountRouted |
				ProcessingState::CallbackGuCFirmwareUpdateRouted;
			break;
		case CPUInfo::CpuGeneration::Broadwell:
			kextList[KextBDWGraphics].pathNum = arrsize(kextBDWPath);
			progressState |= ProcessingState::CallbackComputeLaneCountRouted |
				ProcessingState::CallbackGuCFirmwareUpdateRouted;
			break;
		case CPUInfo::CpuGeneration::Skylake:
			kextList[KextSKLGraphics].pathNum = arrsize(kextSKLPath);
			kextList[KextSKLGraphicsFramebuffer].pathNum = arrsize(kextSKLFramebufferPath);
			break;
		case CPUInfo::CpuGeneration::KabyLake:
			kextList[KextKBLGraphics].pathNum = arrsize(kextKBLPath);
			kextList[KextKBLGraphicsFramebuffer].pathNum = arrsize(kextKBLFramebufferPath);
			break;
		default:
			SYSLOG("igfx", "found an unsupported processor 0x%X:0x%X, please report this!", family, model);
			progressState |= ProcessingState::CallbackPavpSessionRouted |
				ProcessingState::CallbackComputeLaneCountRouted |
				ProcessingState::CallbackDriverStartRouted |
				ProcessingState::CallbackGuCFirmwareUpdateRouted;
			break;
	}

	if (getKernelVersion() >= KernelVersion::Yosemite)
		kextList[KextIOGraphicsFamily].pathNum = arrsize(kextIOGraphicsPath);
	else
		progressState |= ProcessingState::CallbackFrameBufferInitRouted;

	// Allow GuC firmware patches to be disabled
	if (!(progressState & ProcessingState::CallbackGuCFirmwareUpdateRouted)) {
		int tmp;
		if (getKernelVersion() < KernelVersion::HighSierra && decideLoadScheduler == ReferenceScheduler) {
			SYSLOG("igfx", "IGScheduler4 unsupported before 10.13, disabling GuC!");
			decideLoadScheduler = BasicScheduler;
		} else if (PE_parse_boot_argn("-disablegfxfirmware", &tmp, sizeof(tmp))) {
			SYSLOG("igfx", "-disablegfxfirmware is not necessary with IntelGraphicsFixup!");
			decideLoadScheduler = BasicScheduler;
		} else if (decideLoadScheduler >= TotalSchedulers) {
			SYSLOG("igfx", "invalid igfxfw option, disabling GuC!");
			decideLoadScheduler = BasicScheduler;
		}
	}

	auto error = lilu.onPatcherLoad([](void *user, KernelPatcher &patcher) {
		static_cast<IGFX *>(user)->processKernel(patcher);
	}, this);

	if (error != LiluAPI::Error::NoError) {
		SYSLOG("igfx", "failed to register onPatcherLoad method %d", error);
		return false;
	}

	callbackIgfx = this;

	error = lilu.onKextLoad(kextList, kextListSize, [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
		callbackPatcher = &patcher;
		callbackIgfx->processKext(patcher, index, address, size);
	}, this);

	if (error != LiluAPI::Error::NoError) {
		SYSLOG("igfx", "failed to register onKextLoad method %d", error);
		return false;
	}

	return true;
}

void IGFX::deinit() {
	if (access) {
		IOLockFree(access);
		access = nullptr;
	}
}

void IGFX::lockDeviceAccess() {
	if (callbackIgfx && callbackIgfx->access)
		IOLockLock(callbackIgfx->access);
	// Panic otherwise?
}

void IGFX::unlockDeviceAccess() {
	if (callbackIgfx && callbackIgfx->access)
		IOLockUnlock(callbackIgfx->access);
	// Panic otherwise?
}

bool IGFX::isConnectorLessFrame() {
	DBGLOG("igfx", "checking frame connectors");
	if (callbackIgfx) {
		IORegistryEntry *igpu = nullptr;
		bool hasAMD = false, hasNVIDIA = false;
		callbackIgfx->getDeviceInfo(&igpu, nullptr, nullptr, &hasAMD, &hasNVIDIA);
		if (igpu) {
			auto frame = callbackIgfx->getFramebufferId(igpu, hasAMD, hasNVIDIA);
			DBGLOG("igfx", "received igpu and amd %d nvidia %d frame %08X", hasAMD, hasNVIDIA, frame);
			return CPUInfo::isConnectorLessPlatformId(frame);
		}
	}
	return false;
}

void IGFX::correctGraphicsAudioProperties(IORegistryEntry *obj, bool connectorLess, bool mislabeled) {
	auto hda = OSDynamicCast(IOService, obj);
	if (!hda) {
		SYSLOG("igfx", "incompatible hdau discovered");
		return;
	}

	if (connectorLess) {
		auto pci = OSDynamicCast(IOService, obj->getParentEntry(gIOServicePlane));
		if (pci) {
			DBGLOG("igfx", "received digital audio parent %s", safeString(pci->getName()));
			hda->stop(pci);
			bool success = hda->terminate();
			DBGLOG("igfx", "terminating digital audio with status %d", success);
			// Only return after successful termination.
			// Otherwise at least try to rename stuff.
			if (success)
				return;
		} else {
			SYSLOG("igfx", "failed to find digital audio parent for termination");
		}
	}

	if (mislabeled) {
		DBGLOG("igfx", "fixing audio plane name to HDAU");
		WIOKit::renameDevice(obj, "HDAU");
	}

	if (!hda->getProperty("hda-gfx"))
		hda->setProperty("hda-gfx", OSData::withBytes("onboard-1", sizeof("onboard-1")));
	else
		DBGLOG("igfx", "existing hda-gfx, assuming partial inject");

	// layout-id is heard to be required in rare cases
	if (!hda->getProperty("layout-id")) {
		auto layout = IntelGraphicsAudio::getAnalogLayout();
		DBGLOG("igfx", "fixing layout-id to %d in hdau", layout);
		hda->setProperty("layout-id", &layout, sizeof(layout));
	} else {
		DBGLOG("igfx", "found existing layout-id in hdau");
	}

	// built-in is required for non-renamed devices
	if (!hda->getProperty("built-in")) {
		DBGLOG("igfx", "fixing built-in in hdau");
		uint8_t builtBytes[] { 0x00 };
		hda->setProperty("built-in", builtBytes, sizeof(builtBytes));
	} else {
		DBGLOG("igfx", "found existing built-in in hdau");
	}
}

uint32_t IGFX::pavpSessionCallback(void *intelAccelerator, PAVPSessionCommandID_t sessionCommand, uint32_t sessionAppId, uint32_t *a4, bool flag) {
	//DBGLOG("igfx, "pavpCallback: cmd = %d, flag = %d, app = %d, a4 = %s", sessionCommand, flag, sessionAppId, a4 == nullptr ? "null" : "not null");

	if (callbackIgfx && callbackPatcher && callbackIgfx->orgPavpSessionCallback) {
		if (sessionCommand == 4) {
			DBGLOG("igfx", "pavpSessionCallback: enforcing error on cmd 4 (send to ring?)!");
			return 0xE00002D6; // or 0
		}

		return callbackIgfx->orgPavpSessionCallback(intelAccelerator, sessionCommand, sessionAppId, a4, flag);
	}

	SYSLOG("igfx", "callback arrived at nowhere");
	return 0;
}

void IGFX::frameBufferInit(void *that) {
	if (callbackIgfx && callbackPatcher && callbackIgfx->gIOFBVerboseBootPtr && callbackIgfx->orgFrameBufferInit) {
		bool tryBackCopy = callbackIgfx->gotInfo && callbackIgfx->resetFramebuffer != FBRESET;
		// For AMD we do a zero-fill.
		bool zeroFill  = tryBackCopy && callbackIgfx->connectorLessFrame && callbackIgfx->hasExternalAMD;
		auto &info = callbackIgfx->vinfo;

		// Copy back usually happens in a separate call to frameBufferInit
		// Furthermore, v_baseaddr may not be available on subsequent calls, so we have to copy
		if (tryBackCopy && info.v_baseaddr && !callbackIgfx->connectorLessFrame) {
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
		if (tryBackCopy || zeroFill) {
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
					zeroFill = false;
					DBGLOG("igfx", "this display has different mode");
				}
			} else {
				DBGLOG("igfx", "failed to obtain display mode");
				tryBackCopy = false;
				zeroFill = false;
			}
		}

		if (!tryBackCopy) *callbackIgfx->gIOFBVerboseBootPtr = 1;
		callbackIgfx->orgFrameBufferInit(that);
		if (!tryBackCopy) *callbackIgfx->gIOFBVerboseBootPtr = verboseBoot;

		if (fb->fVramMap) {
			auto src = reinterpret_cast<uint8_t *>(callbackIgfx->consoleBuffer);
			auto dst = reinterpret_cast<uint8_t *>(fb->fVramMap->getVirtualAddress());
			if (tryBackCopy) {
				DBGLOG("igfx", "attempting to copy...");
				// Here you can actually draw at your will, but looks like only on Intel.
				lilu_os_memcpy(dst, src, info.v_rowbytes * info.v_height);
			} else if (zeroFill) {
				DBGLOG("igfx", "doing zero-fill...");
				memset(dst, 0, info.v_rowbytes * info.v_height);
			}
		}
	}
}

bool IGFX::computeLaneCount(void *that, void *timing, unsigned int bpp, int availableLanes, int *laneCount) {
	DBGLOG("igfx", "computeLaneCount: bpp = %d, available = %d", bpp, availableLanes);

	bool r = false;
	if (callbackIgfx) {
		// It seems that AGDP fails to properly detect external boot monitors. As a result computeLaneCount
		// is mistakengly called for any boot monitor (e.g. HDMI/DVI), while it is only meant to be used for
		// DP (eDP) displays. More details could be found at:
		// https://github.com/vit9696/Lilu/issues/27#issuecomment-372103559
		// Since the only problematic function is AppleIntelFramebuffer::validateDetailedTiming, there are
		// multiple ways to workaround it.
		// 1. In 10.13.4 Apple added an additional extended timing validation call, which happened to be 
		// guardded by a HDMI 2.0 enable boot-arg, which resulted in one bug fixing the other.
		// 2. Another good way is to intercept AppleIntelFramebufferController::RegisterAGDCCallback and
		// make sure AppleGraphicsDevicePolicy::_VendorEventHandler returns mode 2 (not 0) for event 10.
		// 3. Disabling AGDC by nopping AppleIntelFramebufferController::RegisterAGDCCallback is also fine.
		// Simply returning true from computeLaneCount and letting 0 to be compared against zero so far was
		// least destructive and most reliable. Let's stick with it until we could solve more problems.
		r = callbackIgfx->orgComputeLaneCount(that, timing, bpp, availableLanes, laneCount);
		if (!callbackIgfx->connectorLessFrame && !r && *laneCount == 0) {
			DBGLOG("igfx", "reporting worked lane count");
			r = true;
		}
	}

	return r;
}

bool IGFX::intelGraphicsStart(IOService *that, IOService *provider) {
	int tmp;
	if (!callbackIgfx || PE_parse_boot_argn("-igfxvesa", &tmp, sizeof(tmp))) {
		DBGLOG("igfx", "prevent starting controller");
		return false;
	}

	// By default Apple drivers load Apple-specific firmware, which is incompatible.
	// On KBL they do it unconditionally, which causes infinite loop.
	// On 10.13 there is an option to ignore/load a generic firmware, which we set here.
	// On 10.12 it is not necessary.
	auto dev = OSDynamicCast(OSDictionary, that->getProperty("Development"));
	if (dev && dev->getObject("GraphicsSchedulerSelect")) {
		auto newDev = OSDynamicCast(OSDictionary, dev->copyCollection());
		if (newDev) {
			uint32_t sched = 2; // force disable via plist
			if (callbackIgfx->decideLoadScheduler == ReferenceScheduler)
				sched = 4; // force reference scheduler
#ifdef IGFX_APPLE_SCHEDULER
			else if (callbackIgfx->decideLoadScheduler == AppleScheduler ||
					 callbackIgfx->decideLoadScheduler == AppleCustomScheduler)
				sched = 3; // force apple scheduler
#endif
			DBGLOG("igfx", "forcing scheduler preference %d", sched);
			newDev->setObject("GraphicsSchedulerSelect", OSNumber::withNumber(sched, 32));
			that->setProperty("Development", newDev);
		}
	}

	int gl = provider->getProperty("disable-metal") != nullptr;
	PE_parse_boot_argn("igfxgl", &gl, sizeof(gl));

	if (gl) {
		DBGLOG("igfx", "disabling metal support");
		that->removeProperty("MetalPluginClassName");
		that->removeProperty("MetalPluginName");
		that->removeProperty("MetalStatisticsName");
	}

	return callbackIgfx->orgGraphicsStart(that, provider);
}

bool IGFX::loadGuCBinary(void *that, bool flag) {
	bool r = false;
	if (callbackIgfx) {
		DBGLOG("igfx", "attempting to load firmware for %d scheduler for cpu gen %d",
			   callbackIgfx->decideLoadScheduler, callbackIgfx->cpuGeneration);

#ifdef IGFX_APPLE_SCHEDULER
		// Go with for testing if requested.
		if (callbackIgfx->decideLoadScheduler == AppleCustomScheduler)
			return callbackIgfx->loadCustomBinary(that, flag);
#endif

		// Reset binary indexes
		callbackIgfx->currentBinaryIndex = -1;
		callbackIgfx->currentDmaIndex = -1;

		// This is required to skip the ME hash verification loop...
		bool shouldUnsetIONDrvMode = false;

#ifdef IGFX_APPLE_SCHEDULER
		// firmwareSizePointer is only required for IGScheduler4
		if (callbackIgfx->decideLoadScheduler == AppleScheduler) {
			auto intelAccelerator = static_cast<uint8_t **>(that)[2];
			if (!(intelAccelerator[4096] & 0x10)) {
				DBGLOG("igfx", "loadBinary setting feature bit 0x10 in 0x%02X", intelAccelerator[4096]);
				intelAccelerator[4096] |= 0x10;
				shouldUnsetIONDrvMode = true;
			}
			// Ensure Apple springboard is disabled
			if (callbackIgfx->canUseSpringboard) {
				DBGLOG("igfx", "making sure springboard will not be loaded %d",
					callbackIgfx->canUseSpringboard ? 1 : 0);
				*callbackIgfx->canUseSpringboard = false;
			}

			callbackIgfx->performingFirmwareLoad = true;
		}
#endif

		if (callbackIgfx->firmwareSizePointer) {
			callbackIgfx->performingFirmwareLoad = true;
		}

		r = callbackIgfx->orgLoadGuCBinary(that, flag);
		DBGLOG("igfx", "loadGuCBinary returned %d", r);

		// Currently AppleScheduler does not work, for some reason after loading HuC and GuC firmwares
		// we receive 0x800071ec in GUC_STATUS (0xC000) register, which unfortunately is not documented:
		// https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/intel_guc_reg.h
		// This happens right after IGGuC::actionResponseWait(this);
		// So IGGuC::sendHostToGucMessage() is not called, and HuC firmware is not verified.
		// The comparison of IGGuC::dmaHostToGuC with difference patching did not find any notable changes.
		// In particular setting the 0x10 bit does not appear to be relevant to the issue.

		// Recover the original value
		if (shouldUnsetIONDrvMode) {
			auto intelAccelerator = static_cast<uint8_t **>(that)[2];
			intelAccelerator[4096] &= ~0x10;
		}

		callbackIgfx->performingFirmwareLoad = false;

		// Reset binary indexes once again just in case
		callbackIgfx->currentBinaryIndex = -1;
		callbackIgfx->currentDmaIndex = -1;
	}
	return r;
}

bool IGFX::loadFirmware(IOService *that) {
	bool r = false;
	if (callbackIgfx) {
		DBGLOG("igfx", "load firmware setting sleep overrides %d", callbackIgfx->cpuGeneration);
		// We have to patch the virtual table, because the original methods are very short.
		// See __ZN12IGScheduler415systemWillSleepEv and __ZN12IGScheduler413systemDidWakeEv
		// Note, that other methods are also not really implemented, so we may have to implement them ourselves sooner or later.
		(*reinterpret_cast<uintptr_t **>(that))[52] = reinterpret_cast<uintptr_t>(systemWillSleep);
		(*reinterpret_cast<uintptr_t **>(that))[53] = reinterpret_cast<uintptr_t>(systemDidWake);
		r = callbackIgfx->orgLoadFirmware(that);
	}
	return r;
}

void IGFX::systemWillSleep(IOService *that) {
	DBGLOG("igfx", "systemWillSleep GuC callback");
	// Perhaps we want to send a message to GuC firmware like Apple does for its own implementation?
}

void IGFX::systemDidWake(IOService *that) {
	DBGLOG("igfx", "systemDidWake GuC callback");
	if (callbackIgfx) {
		// This is IGHardwareGuC class instance.
		auto &GuC = (reinterpret_cast<OSObject **>(that))[76];
		DBGLOG("igfx", "reloading firmware on wake discovered IGHardwareGuC %d", GuC ? 1 : 0);
		if (GuC) {
			GuC->release();
			GuC = nullptr;
		}
		callbackIgfx->orgLoadFirmware(that);
	}
}

bool IGFX::initSchedControl(void *that, void *ctrl) {
	bool r = false;
	if (callbackIgfx) {
		// This function is caled within loadGuCBinary, and it also uses shared buffers.
		// To avoid any issues here we ensure that the filtering is off.
		DBGLOG("igfx", "attempting to init sched control with load %d", callbackIgfx->performingFirmwareLoad);
		bool perfLoad = callbackIgfx->performingFirmwareLoad;
		callbackIgfx->performingFirmwareLoad = false;
		r = callbackIgfx->orgInitSchedControl(that, ctrl);

		if (callbackIgfx->decideLoadScheduler == ReferenceScheduler) {
			struct ParamRegs {
				uint32_t bak[35];
				uint32_t params[10];
			};

#ifdef DEBUG
			auto v = &static_cast<ParamRegs *>(that)->params[0];
			DBGLOG("igfx", "fw params1 %08X %08X %08X %08X %08X", v[0], v[1], v[2], v[3], v[4]);
			DBGLOG("igfx", "fw params2 %08X %08X %08X %08X %08X", v[5], v[6], v[7], v[8], v[9]);
#endif
		}

		callbackIgfx->performingFirmwareLoad = perfLoad;
	}
	return r;
}

void *IGFX::igBufferWithOptions(void *accelTask, unsigned long size, unsigned int type, unsigned int flags) {
	void *r = nullptr;
	if (callbackIgfx) {
		// Here we try to determine which binary this is.
		bool shouldIntercept = callbackIgfx->performingFirmwareLoad;
		if (shouldIntercept) {
			++callbackIgfx->currentBinaryIndex;
			shouldIntercept = callbackIgfx->currentBinaryIndex < arrsize(callbackIgfx->binaryInterception) &&
				callbackIgfx->binaryInterception[callbackIgfx->currentBinaryIndex];
		}

		if (shouldIntercept) {
			// Allocate a dummy buffer
			auto currIndex = callbackIgfx->currentBinaryIndex;
			callbackIgfx->dummyFirmwareBuffer[currIndex] = Buffer::create<uint8_t>(size);
			// Select the latest firmware to upload
			DBGLOG("igfx", "preparing firmware index %d for cpu gen %d with range 0x%lX",
				   currIndex, callbackIgfx->cpuGeneration, size);

			const void *fw = nullptr;
			const void *fwsig = nullptr;
			size_t fwsize = 0;
			size_t fwsigsize = 0;

			if (currIndex == 0) {
				// GuC comes first
				if (callbackIgfx->cpuGeneration == CPUInfo::CpuGeneration::Skylake) {
					fw = GuCFirmwareSKL;
					fwsig = GuCFirmwareSKLSignature;
					fwsize = GuCFirmwareSKLSize;
					fwsigsize = GuCFirmwareSignatureSize;
				} else {
					fw = GuCFirmwareKBL;
					fwsig = GuCFirmwareKBLSignature;
					fwsize = GuCFirmwareKBLSize;
					fwsigsize = GuCFirmwareSignatureSize;
				}
			} else if (currIndex == 1) {
				// HuC comes next
				if (callbackIgfx->cpuGeneration == CPUInfo::CpuGeneration::Skylake) {
					fw = HuCFirmwareSKL;
					fwsig = HuCFirmwareSKLSignature;
					fwsize = HuCFirmwareSKLSize;
					fwsigsize = HuCFirmwareSignatureSize;
				} else {
					fw = HuCFirmwareKBL;
					fwsig = HuCFirmwareKBLSignature;
					fwsize = HuCFirmwareKBLSize;
					fwsigsize = HuCFirmwareSignatureSize;
				}
			}

			// Allocate enough memory for the new firmware (should be 64K-aligned)
			unsigned long newsize = fwsize > size ? ((fwsize + 0xFFFF) & (~0xFFFF)) : size;
			r = callbackIgfx->orgIgBufferWithOptions(accelTask, newsize, type, flags);
			// Replace the real buffer with a dummy buffer
			if (r && callbackIgfx->dummyFirmwareBuffer[currIndex]) {
				// Copy firmware contents, update the sizes and signature
				auto status = MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock);
				if (status == KERN_SUCCESS) {
					// Upload the firmware ourselves
					callbackIgfx->realFirmwareBuffer[currIndex] = static_cast<uint8_t **>(r)[7];
					static_cast<uint8_t **>(r)[7] = callbackIgfx->dummyFirmwareBuffer[currIndex];
					lilu_os_memcpy(callbackIgfx->realFirmwareBuffer[currIndex], fw, fwsize);
					lilu_os_memcpy(callbackIgfx->signaturePointer[currIndex], fwsig, fwsigsize);
					callbackIgfx->realBinarySize[currIndex] = static_cast<uint32_t>(fwsize);
					// Update the firmware size for IGScheduler4
					if (callbackIgfx->firmwareSizePointer)
						*callbackIgfx->firmwareSizePointer = static_cast<uint32_t>(fwsize);
					MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
				} else {
					SYSLOG("igfx", "ig buffer protection upgrade failure %d", status);
				}
			} else if (callbackIgfx->dummyFirmwareBuffer[currIndex]) {
				SYSLOG("igfx", "ig shared buffer allocation failure");
				Buffer::deleter(callbackIgfx->dummyFirmwareBuffer[currIndex]);
				callbackIgfx->dummyFirmwareBuffer[currIndex] = nullptr;
			} else {
				SYSLOG("igfx", "dummy buffer allocation failure");
			}

		} else {
			r = callbackIgfx->orgIgBufferWithOptions(accelTask, size, type, flags);
		}
	}
	return r;
}

uint64_t IGFX::igBufferGetGpuVirtualAddress(void *that) {
	uint64_t r = 0;
	if (callbackIgfx) {
		auto currIndex = callbackIgfx->currentBinaryIndex;
		bool didIntercept = callbackIgfx->performingFirmwareLoad;
		if (didIntercept) {
			didIntercept = currIndex < arrsize(callbackIgfx->binaryInterception) &&
				callbackIgfx->binaryInterception[currIndex];
		}

		if (didIntercept && callbackIgfx->realFirmwareBuffer[currIndex]) {
			// Restore the original framebuffer
			static_cast<uint8_t **>(that)[7] = callbackIgfx->realFirmwareBuffer[currIndex];
			callbackIgfx->realFirmwareBuffer[currIndex] = nullptr;
			// Free the dummy framebuffer which is no longer used
			Buffer::deleter(callbackIgfx->dummyFirmwareBuffer[currIndex]);
			callbackIgfx->dummyFirmwareBuffer[currIndex] = nullptr;
			r = callbackIgfx->orgIgGetGpuVirtualAddress(that);
			DBGLOG("igfx", "saving gpu address %08X for firmware %d", static_cast<uint32_t>(r), currIndex);
			callbackIgfx->gpuFirmwareAddress[currIndex] = r;
		} else {
			r = callbackIgfx->orgIgGetGpuVirtualAddress(that);
		}
	}
	return r;
}

bool IGFX::dmaHostToGuC(void *that, uint64_t gpuAddr, uint32_t gpuReg, uint32_t dataLen, uint32_t dmaType, bool unk) {
	bool r = false;
	if (callbackIgfx) {
		callbackIgfx->currentDmaIndex++;
		DBGLOG("igfx", "dma host -> guc to reg 0x%04X with size %04X dma %d unk %d dma index %d",
			   gpuReg, dataLen, dmaType, unk, callbackIgfx->currentDmaIndex);
		// HuC then GuC.
		auto currIndex = callbackIgfx->currentDmaIndex;
		if (currIndex < 2) {

			if (currIndex == 0) {
				auto fIntelAccelerator = getMember<void *>(that, 0x10);
				auto fIGAccelTask = getMember<void *>(fIntelAccelerator, 0x160);

				auto logContext = callbackIgfx->orgIgBufferWithOptions(fIGAccelTask, 0x12000, 0x1A, 0);
				if (!logContext) {
					PANIC("igfx", "failed to allocate log context");
				}

				uint32_t params[10] {};
				params[GUC_CTL_ARAT_HIGH] = 0;
				params[GUC_CTL_ARAT_LOW] = /* 100000000 */ 4000003;

				// params[GUC_CTL_CTXINFO] = (orgIgGetGpuVirtualAddress(igContext) & 0xFFFFF000) | 0x40;
				params[GUC_CTL_LOG_PARAMS] = (callbackIgfx->orgIgGetGpuVirtualAddress(logContext) & 0xFFFFF000) | 0xFC3;

				//params[GUC_CTL_CTXINFO] = (orgIgGetGpuVirtualAddress(getMember<void *>(that, 0x80500)) & 0xFFFFF000) | 0x40;
				// GPU_TYPE is 0, 1, 9, 0xA
				params[GUC_CTL_DEVICE_INFO] = (GUC_CORE_FAMILY_GEN9 << GUC_CTL_CORE_FAMILY_SHIFT) | 1;
				params[GUC_CTL_WA] = GUC_CTL_WA_UK_BY_DRIVER;
				params[GUC_CTL_FEATURE] = /* GUC_CTL_VCS2_ENABLED | */ GUC_CTL_KERNEL_SUBMISSIONS;

				// That's what IGScheduler4 does.
				mmioWrite(that, SOFT_SCRATCH(0), 0);
				for (uint32_t i = 1; i < 10; i++) // skip context
					mmioWrite(that, SOFT_SCRATCH(1 + i), params[i]);
			}

			gpuAddr = callbackIgfx->gpuFirmwareAddress[1-currIndex];
			gpuReg = currIndex == 0 ? 0 : 0x2000;
			dataLen = callbackIgfx->realBinarySize[1-currIndex];
			dmaType = currIndex == 0 ? HUC_UKERNEL : UOS_MOVE;
			DBGLOG("igfx", "dmaHostToGuC replaced with size %04X", dataLen);

			return doDmaTransfer(that, gpuAddr, gpuReg, dataLen, dmaType);
		} else {
			r = callbackIgfx->orgDmaHostToGuC(that, gpuAddr, gpuReg, dataLen, dmaType, unk);
		}

		DBGLOG("igfx", "dmaHostToGuC returned %d", r);
	}

	return r;
}

void IGFX::initInterruptServices(void *that) {
	if (callbackIgfx) {
		DBGLOG("igfx", "init interrupt services");
		callbackIgfx->orgInitInterruptServices(that);
		callbackIgfx->resetFirmware(that);
	}
}

uint16_t IGFX::configRead16(IORegistryEntry *service, uint32_t space, uint8_t offset) {
	if (callbackIgfx && callbackIgfx->orgConfigRead16) {
		auto result = callbackIgfx->orgConfigRead16(service, space, offset);
		if (offset == WIOKit::kIOPCIConfigDeviceID && service != nullptr) {
			auto name = service->getName();
			if (name && name[0] == 'I' && name[1] == 'G' && name[2] == 'P' && name[3] == 'U') {
				DBGLOG("igfx", "configRead16 IGPU 0x%08X at off 0x%02X, result = 0x%04x", space, offset, result);
				uint32_t device;
				if (WIOKit::getOSDataValue(service, "device-id", device) && device != result) {
					DBGLOG("igfx", "configRead16 IGPU reported 0x%04x instead of 0x%04x", device, result);
					return device;
				}
			}
		}

		return result;
	}

	return 0;
}

uint32_t IGFX::configRead32(IORegistryEntry *service, uint32_t space, uint8_t offset) {
	if (callbackIgfx && callbackIgfx->orgConfigRead32) {
		auto result = callbackIgfx->orgConfigRead32(service, space, offset);
		// According to lvs unaligned reads may happen
		if ((offset == WIOKit::kIOPCIConfigDeviceID || offset == WIOKit::kIOPCIConfigVendorID) && service != nullptr) {
			auto name = service->getName();
			if (name && name[0] == 'I' && name[1] == 'G' && name[2] == 'P' && name[3] == 'U') {
				DBGLOG("igfx", "configRead32 IGPU 0x%08X at off 0x%02X, result = 0x%08X", space, offset, result);
				uint32_t device;
				if (WIOKit::getOSDataValue(service, "device-id", device) && device != (result & 0xFFFF)) {
					device = (result & 0xFFFF) | (device << 16);
					DBGLOG("igfx", "configRead32 reported 0x%08x instead of 0x%08x", device, result);
					return device;
				}
			}
		}

		return result;
	}

	return 0;
}

uint32_t IGFX::mmioRead(void *fw, uint32_t reg) {
	auto fIntelAccelerator = getMember<void *>(fw, 0x10);
	auto mmio = getMember<volatile uint32_t *>(fIntelAccelerator, 0x1090);
	return mmio[reg/4];
}

void IGFX::mmioWrite(void *fw, uint32_t reg, uint32_t v) {
	auto fIntelAccelerator = getMember<void *>(fw, 0x10);
	auto mmio = getMember<volatile uint32_t *>(fIntelAccelerator, 0x1090);
	mmio[reg/4] = v;
}

bool IGFX::doDmaTransfer(void *that, uint64_t gpuAddr, uint32_t gpuReg, uint32_t dataLen, uint32_t dmaType) {
	DBGLOG("igfx", "doDmaTransfer with size %04X, status %08X", dataLen, mmioRead(that, GUC_STATUS));

	mmioWrite(that, DMA_COPY_SIZE, dataLen);
	mmioWrite(that, DMA_ADDR_0_LOW, static_cast<uint32_t>(gpuAddr));
	mmioWrite(that, DMA_ADDR_0_HIGH, static_cast<uint32_t>(gpuAddr >> 32) & 0xFFFF);
	mmioWrite(that, DMA_ADDR_1_LOW, gpuReg);
	mmioWrite(that, DMA_ADDR_1_HIGH, DMA_ADDRESS_SPACE_WOPCM);

	mmioWrite(that, GEN8_GTCR, GEN8_GTCR_INVALIDATE);
	while (mmioRead(that, GEN8_GTCR) & GEN8_GTCR_INVALIDATE);

	mmioWrite(that, DMA_CTRL, 0xFFFF0000 | dmaType | START_DMA);

	uint32_t status = 0, i = 0;
	while (i < 1500) {
		uint32_t nstatus = mmioRead(that, DMA_CTRL);
		if (nstatus != status)
			DBGLOG("igfx", "doDmaTransfer dma_ctrl change to %08X", nstatus);
		nstatus = status;

		if ((nstatus & START_DMA) != START_DMA)
			break;

		IOSleep(1);
		i++;
	}

	DBGLOG("igfx", "doDmaTransfer complete with %d cycles, dma_ctrl: %08X, status: %08X", i, status, mmioRead(that, GUC_STATUS));

	status = 0;
	i = 0;
	while (i < 1500) {
		uint32_t nstatus = mmioRead(that, GUC_STATUS);
		if (nstatus != status)
			DBGLOG("igfx", "doDmaTransfer status change to %08X", nstatus);
		status = nstatus;

		if ((status & GS_UKERNEL_MASK) == GS_UKERNEL_READY)
			break;

		IOSleep(1);
		i++;
	}

	// This is what Linux does, also, it will not be 0xFFFF0000, but actually dmaType << 16 here and above.
	// mmioWrite(that, DMA_CTRL, 0xFFFF0000);

	DBGLOG("igfx", "doDmaTransfer verified with %d cycles and %08X status", i, status);

	return ((status & GS_UKERNEL_MASK) == GS_UKERNEL_READY);
}

void IGFX::resetFirmware(void *that) {
	DBGLOG("igfx", "before reset firmware status: %08X, reset: %08X", mmioRead(that, GUC_STATUS), mmioRead(that, GEN6_GDRST));

	mmioWrite(that, GEN6_GDRST, GEN9_GRDOM_GUC);
	uint32_t status = 0, i = 0;
	while (i < 1500) {
		status = mmioRead(that, GEN6_GDRST);

		if ((status & GEN9_GRDOM_GUC) == 0)
			break;

		IOSleep(1);
		i++;
	}

	DBGLOG("igfx", "after reset firmware status: %08X, reset: %08X, cycles: %d", mmioRead(that, GUC_STATUS), status, i);
}

bool IGFX::loadCustomBinary(void *that, bool restore) {

	auto fIGGuCCtrl = getMember<void *>(that, 0x258);
	auto fIntelAccelerator = getMember<void *>(that, 0x10);
	auto fIGAccelTask = getMember<void *>(fIntelAccelerator, 0x160);

	if (!restore)
		orgInitSchedControl(that, fIGGuCCtrl);

	const void *gucfw = nullptr, *gucfwsig = nullptr, *hucfw = nullptr, *hucfwsig = nullptr;
	size_t gucfwsize = 0, hucfwsize = 0;

	if (cpuGeneration == CPUInfo::CpuGeneration::Skylake) {
		gucfw = GuCFirmwareSKL;
		gucfwsig = GuCFirmwareSKLSignature;
		hucfw = HuCFirmwareSKL;
		hucfwsig = HuCFirmwareSKLSignature;
		gucfwsize = GuCFirmwareSKLSize;
		hucfwsize = HuCFirmwareSKLSize;
	} else {
		gucfw = GuCFirmwareKBL;
		gucfwsig = GuCFirmwareKBLSignature;
		hucfw = HuCFirmwareKBL;
		hucfwsig = HuCFirmwareKBLSignature;
		gucfwsize = GuCFirmwareKBLSize;
		hucfwsize = HuCFirmwareKBLSize;
	}

	uint64_t igGuCAddr = 0, igHuCAddr = 0, igHuCSigAddr = 0;

	auto igContext = orgIgBufferWithOptions(fIGAccelTask, 0x45000, 0x18, 0);
	if (!igContext) {
		PANIC("igfx", "failed to allocate ig context");
	}

	auto logContext = orgIgBufferWithOptions(fIGAccelTask, 0x12000, 0x1A, 0);
	if (!logContext) {
		PANIC("igfx", "failed to allocate log context");
	}

	auto igGuCBuffer = orgIgBufferWithOptions(fIGAccelTask, alignValue(gucfwsize), 0x1A, 0);
	if (igGuCBuffer) {
		DBGLOG("igfx", "preparing GuC firmware %ld", gucfwsize);
		auto vaddr = static_cast<uint8_t **>(igGuCBuffer)[7];
		lilu_os_memcpy(vaddr, gucfw, gucfwsize);
		igGuCAddr = orgIgGetGpuVirtualAddress(igGuCBuffer);
	}

	auto igHuCBuffer = orgIgBufferWithOptions(fIGAccelTask, alignValue(hucfwsize), 0x1A, 0);
	if (igHuCBuffer) {
		DBGLOG("igfx", "preparing HuC firmware %ld", hucfwsize);
		auto vaddr = static_cast<uint8_t **>(igHuCBuffer)[7];
		lilu_os_memcpy(vaddr, hucfw, hucfwsize);
		igHuCAddr = orgIgGetGpuVirtualAddress(igHuCBuffer);
	}

	auto igHuCSigBuffer = orgIgBufferWithOptions(fIGAccelTask, alignValue(HuCFirmwareSignatureSize), 0x1A, 0);
	if (igHuCSigBuffer) {
		DBGLOG("igfx", "preparing HuC firmware sig %ld", HuCFirmwareSignatureSize);
		auto vaddr = static_cast<uint8_t **>(igHuCSigBuffer)[7];
		lilu_os_memcpy(vaddr, hucfwsig, HuCFirmwareSignatureSize);
		igHuCSigAddr = orgIgGetGpuVirtualAddress(igHuCSigBuffer);
	}

	if (!igGuCAddr || !igHuCAddr || !igHuCSigAddr) {
		PANIC("igfx", "failed to allocate firmware buffers");
	}

	orgSafeForceWake(fIntelAccelerator, 1, 7);

	DBGLOG("igfx", "initial firmware status is %08X", mmioRead(that, GUC_STATUS));

	orgInitInterruptServices(that);

	for (uint32_t tries = 0; tries < 3; tries++) {
		resetFirmware(that);

		mmioWrite(that, 0x1984, 1);
		mmioWrite(that, GEN9_GT_PM_CONFIG, GT_DOORBELL_ENABLE);
		mmioWrite(that, GEN7_MISCCPCTL, GEN8_DOP_CLOCK_GATE_GUC_ENABLE);

		// 0x8607
		mmioWrite(that, GUC_SHIM_CONTROL, GUC_DISABLE_SRAM_INIT_TO_ZEROES |
				  GUC_ENABLE_READ_CACHE_LOGIC |
				  GUC_ENABLE_MIA_CACHING |
				  GUC_ENABLE_READ_CACHE_FOR_SRAM_DATA |
				  GUC_ENABLE_READ_CACHE_FOR_WOPCM_DATA |
				  GUC_ENABLE_MIA_CLOCK_GATING);
		mmioWrite(that, GUC_ARAT_C6DIS, 0x1FF);

		uint32_t params[10] {};

		// SKL 530 and 630 have
		// params1 4002C040 00000000 003D0903 00000601 40071FC3
		// params2 00000000 00000008 00000002 00000000 00000000

		params[GUC_CTL_ARAT_HIGH] = 0;
		params[GUC_CTL_ARAT_LOW] = /* 100000000 */ 4000003;

		params[GUC_CTL_CTXINFO] = (orgIgGetGpuVirtualAddress(igContext) & 0xFFFFF000) | 0x40;
		params[GUC_CTL_LOG_PARAMS] = (orgIgGetGpuVirtualAddress(logContext) & 0xFFFFF000) | 0xFC3;

		//params[GUC_CTL_CTXINFO] = (orgIgGetGpuVirtualAddress(getMember<void *>(that, 0x80500)) & 0xFFFFF000) | 0x40;
		// GPU_TYPE is 0, 1, 9, 0xA
		params[GUC_CTL_DEVICE_INFO] = (GUC_CORE_FAMILY_GEN9 << GUC_CTL_CORE_FAMILY_SHIFT) | 1;
		params[GUC_CTL_WA] = GUC_CTL_WA_UK_BY_DRIVER;
		params[GUC_CTL_FEATURE] = /* GUC_CTL_VCS2_ENABLED | */ GUC_CTL_KERNEL_SUBMISSIONS;

		// That's what IGScheduler4 does.
		mmioWrite(that, SOFT_SCRATCH(0), 0);
		for (uint32_t i = 0; i < 10; i++)
			mmioWrite(that, SOFT_SCRATCH(1 + i), params[i]);

		for (uint32_t i = 0; i < UOS_RSA_SCRATCH_COUNT; i++)
			mmioWrite(that, UOS_RSA_SCRATCH(i), reinterpret_cast<const uint32_t *>(gucfwsig)[i]);

		// From Linux
		//mmioWrite(that, DMA_GUC_WOPCM_OFFSET, GUC_WOPCM_OFFSET_VALUE | HUC_LOADING_AGENT_GUC);
		//mmioWrite(that, GUC_WOPCM_SIZE, GUC_WOPCM_TOP);

		// From Apple
		mmioWrite(that, DMA_GUC_WOPCM_OFFSET, GUC_WOPCM_OFFSET_VALUE+1);
		mmioWrite(that, GUC_WOPCM_SIZE, GUC_WOPCM_TOP+1);

		bool r = doDmaTransfer(that, igGuCAddr, 0x2000, static_cast<uint32_t>(gucfwsize), UOS_MOVE);
		if (r) break;

		DBGLOG("igfx", "retrying due to failure");

		IOSleep(3000);
	}


	orgSafeForceWake(fIntelAccelerator, 0, 7);

	return false;
}

void IGFX::processKernel(KernelPatcher &patcher) {
	// We need to load vinfo in all cases but reset
	if (resetFramebuffer != FBRESET) {
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

	lockDeviceAccess();
	correctDeviceProperties();
	unlockDeviceAccess();
}

void IGFX::getDeviceInfo(IORegistryEntry **igpu, IORegistryEntry **imei, IORegistryEntry **hdau, bool *hasAMD, bool *hasNVIDIA) {
	// Ensure the values are initialised
	if (igpu) *igpu = nullptr;
	if (imei) *imei = nullptr;
	if (hdau) *hdau = nullptr;
	if (hasAMD) *hasAMD = false;
	if (hasNVIDIA) *hasNVIDIA = false;

	// Detect all the devices
	auto sect = WIOKit::findEntryByPrefix("/AppleACPIPlatformExpert", "PCI", gIOServicePlane);
	if (sect) sect = WIOKit::findEntryByPrefix(sect, "AppleACPIPCI", gIOServicePlane);
	if (sect) {
		auto iterator = sect->getChildIterator(gIOServicePlane);
		if (iterator) {
			IORegistryEntry *obj = nullptr;
			while ((obj = OSDynamicCast(IORegistryEntry, iterator->getNextObject())) != nullptr) {
				uint32_t vendor = 0, code = 0;
				if (WIOKit::getOSDataValue(obj, "vendor-id", vendor) && vendor == WIOKit::VendorID::Intel &&
					WIOKit::getOSDataValue(obj, "class-code", code)) {
					auto name = obj->getName();
					if (igpu && (code == WIOKit::ClassCode::DisplayController || code == WIOKit::ClassCode::VGAController)) {
						*igpu = obj;
					} else if ((hasAMD || hasNVIDIA) && code == WIOKit::ClassCode::PCIBridge) {
						DBGLOG("igfx", "found pci bridge %s", safeString(name));
						auto gpuiterator = IORegistryIterator::iterateOver(obj, gIOServicePlane, kIORegistryIterateRecursively);
						if (gpuiterator) {
							IORegistryEntry *gpuobj = nullptr;
							while ((gpuobj = OSDynamicCast(IORegistryEntry, gpuiterator->getNextObject())) != nullptr) {
								uint32_t gpuvendor = 0, gpucode = 0;
								DBGLOG("igfx", "found %s on pci bridge", safeString(gpuobj->getName()));
								if (WIOKit::getOSDataValue(gpuobj, "vendor-id", gpuvendor) &&
									WIOKit::getOSDataValue(gpuobj, "class-code", gpucode) &&
									(gpucode == WIOKit::ClassCode::DisplayController || gpucode == WIOKit::ClassCode::VGAController)) {
									if (gpuvendor == WIOKit::VendorID::ATIAMD) {
										DBGLOG("igfx", "found AMD GPU device %s", safeString(gpuobj->getName()));
										if (hasAMD) *hasAMD = true;
										break;
									} else if (gpuvendor == WIOKit::VendorID::NVIDIA) {
										DBGLOG("igfx", "found NVIDIA GPU device %s", safeString(gpuobj->getName()));
										if (hasNVIDIA) *hasNVIDIA = true;
										break;
									}
								}
							}
							gpuiterator->release();
						}
					} else if (imei && (code == WIOKit::ClassCode::IMEI || (name &&
						(!strcmp(name, "IMEI") || !strcmp(name, "HECI") || !strcmp(name, "MEI"))))) {
						// Fortunately IMEI is always made by Intel
						DBGLOG("igfx", "found IMEI device %s", safeString(name));
						*imei = obj;
					} else if (hdau && name && (!strcmp(name, "HDAU") || !strcmp(name, "B0D3"))) {
						DBGLOG("igfx", "found HDAU device %s", safeString(name));
						*hdau = obj;
					}
				}
			}

			iterator->release();
		}
	}
}

void IGFX::correctDeviceProperties() {
	// Detect all the devices
	IORegistryEntry *igpu = nullptr, *imei = nullptr, *hdau = nullptr;
	getDeviceInfo(&igpu, &imei, &hdau, &hasExternalAMD, &hasExternalNVIDIA);

	if (igpu)
		correctGraphicsProperties(igpu, igpu->getName());

	if (hdau) {
		// Haswell and Broadwell have a dedicated device for digital audio source.
		// In some cases HDAU probing may fail to detect external GPUs.
		// Here we try to workaround it by redoing property correction.
		auto name = hdau->getName();
		correctGraphicsAudioProperties(hdau, connectorLessFrame, !name || strcmp(name, "HDAU"));
	}

	if (imei) {
		auto name = imei->getName();
		// Rename mislabeled IMEI device
		if (!name || strcmp(name, "IMEI"))
			WIOKit::renameDevice(imei, "IMEI");

		uint32_t device = 0;
		if ((cpuGeneration == CPUInfo::CpuGeneration::SandyBridge ||
			cpuGeneration == CPUInfo::CpuGeneration::IvyBridge) &&
			WIOKit::getOSDataValue(imei, "device-id", device)) {
			// Exotic cases like SNB CPU on 7-series motherboards or IVB CPU on 6-series
			// require device-id faking. Unfortunately it is too late to change it at this step,
			// because device matching happens earlier, but we will spill a warning to make sure
			// one fixes them at device property or ACPI level.
			uint32_t suggest = 0;
			if (cpuGeneration == CPUInfo::CpuGeneration::SandyBridge && device != 0x1C3A)
				suggest = 0x1C3A;
			else if (cpuGeneration == CPUInfo::CpuGeneration::IvyBridge && device != 0x1E3A)
				suggest = 0x1E3A;

			if (suggest != 0) {
				uint8_t bus = 0, dev = 0, fun = 0;
				WIOKit::getDeviceAddress(imei, bus, dev, fun);
				SYSLOG("igfx", "IMEI device (%02X:%02X.%02X) has device-id 0x%04X, you should change it to 0x%04X",
					   bus, dev, fun, device, suggest);
			}
		}
	}
}

uint32_t IGFX::getFramebufferId(IORegistryEntry *igpu, bool hasAMD, bool hasNVIDIA, bool update) {
	uint32_t platform = CPUInfo::DefaultInvalidPlatformId;
	if (PE_parse_boot_argn("igfxframe", &platform, sizeof(platform))) {
		SYSLOG("igfx", "found TEST frame override %08X", platform);
	} else {
		bool specified = false;
		platform = CPUInfo::getGpuPlatformId(igpu, &specified);
		if (!specified && (cpuGeneration == CPUInfo::CpuGeneration::Skylake ||
			cpuGeneration == CPUInfo::CpuGeneration::KabyLake)) {
			DBGLOG("igfx", "discarding default framebuffer for SKL/KBL in favour of our own");
			platform = CPUInfo::DefaultInvalidPlatformId;
		}
		if (platform == CPUInfo::DefaultInvalidPlatformId) {
			uint8_t bus = 0, dev = 0, fun = 0;
			WIOKit::getDeviceAddress(igpu, bus, dev, fun);
			DBGLOG("igfx", "IGPU device (%02X:%02X.%02X) has no framebuffer id, falling back to defaults", bus, dev, fun);

			// There is no connector-less frame in Broadwerll
			if ((hasAMD || hasNVIDIA) && cpuGeneration != CPUInfo::CpuGeneration::Broadwell) {
				DBGLOG("igfx", "discovered external AMD or NVIDIA, using frame without connectors");
				// Note, that setting non-standard connector-less frame may result in 2 GPUs visible
				// in System Report for whatever reason (at least on KabyLake).
				if (cpuGeneration == CPUInfo::CpuGeneration::SandyBridge)
					platform = CPUInfo::ConnectorLessSandyBridgePlatformId2;
				else if (cpuGeneration == CPUInfo::CpuGeneration::IvyBridge)
					platform = CPUInfo::ConnectorLessIvyBridgePlatformId2;
				else if (cpuGeneration == CPUInfo::CpuGeneration::Haswell)
					platform = CPUInfo::ConnectorLessHaswellPlatformId1;
				else if (cpuGeneration == CPUInfo::CpuGeneration::Skylake)
					platform = CPUInfo::ConnectorLessSkylakePlatformId3;
				else if (cpuGeneration == CPUInfo::CpuGeneration::KabyLake)
					platform = CPUInfo::ConnectorLessKabyLakePlatformId2;
			} else {
				// These are really failsafe defaults, you should NOT rely on them.
				auto model = WIOKit::getComputerModel();
				if (model == WIOKit::ComputerModel::ComputerLaptop) {
					if (cpuGeneration == CPUInfo::CpuGeneration::SandyBridge)
						platform = 0x00010000;
					else if (cpuGeneration == CPUInfo::CpuGeneration::IvyBridge)
						platform = 0x01660003;
					else if (cpuGeneration == CPUInfo::CpuGeneration::Haswell)
						platform = 0x0A160000;
					else if (cpuGeneration == CPUInfo::CpuGeneration::Broadwell)
						platform = 0x16260006;
					else if (cpuGeneration == CPUInfo::CpuGeneration::Skylake)
						platform = 0x19160000;
					else if (cpuGeneration == CPUInfo::CpuGeneration::KabyLake)
						platform = 0x591B0000;
				} else {
					if (cpuGeneration == CPUInfo::CpuGeneration::SandyBridge)
						platform = 0x00030010;
					else if (cpuGeneration == CPUInfo::CpuGeneration::IvyBridge)
						platform = 0x0166000A;
					else if (cpuGeneration == CPUInfo::CpuGeneration::Haswell)
						platform = 0x0D220003;
					else if (cpuGeneration == CPUInfo::CpuGeneration::Broadwell)
						platform = 0x16220007;  /* for now */
					else if (cpuGeneration == CPUInfo::CpuGeneration::Skylake)
						platform = CPUInfo::DefaultSkylakePlatformId;
					else if (cpuGeneration == CPUInfo::CpuGeneration::KabyLake)
						platform = CPUInfo::DefaultKabyLakePlatformId;
				}
			}
		}
	}

	if (update) {
		if (cpuGeneration == CPUInfo::CpuGeneration::SandyBridge)
			igpu->setProperty("AAPL,snb-platform-id", &platform, sizeof(platform));
		else
			igpu->setProperty("AAPL,ig-platform-id", &platform, sizeof(platform));
	}

	return platform;
}

void IGFX::correctGraphicsProperties(IORegistryEntry *obj, const char *name) {
	DBGLOG("igfx", "found Intel GPU device %s", safeString(name));
	if (!name || strcmp(name, "IGPU"))
		WIOKit::renameDevice(obj, "IGPU");

	// Model and ID fixes
	uint32_t realDevice = WIOKit::readPCIConfigValue(obj, WIOKit::kIOPCIConfigDeviceID);
	uint32_t acpiDevice = 0, fakeDevice = 0;

	if (!WIOKit::getOSDataValue(obj, "device-id", acpiDevice))
		DBGLOG("igfx", "missing IGPU device-id");

	auto model = getModelName(realDevice, fakeDevice);
	DBGLOG("igfx", "IGPU has real %04X acpi %04X fake %04X and model %s",
		   realDevice, acpiDevice, fakeDevice, safeString(model));

	if (model && !obj->getProperty("model")) {
		DBGLOG("igfx", "adding missing model %s from autotodetect", model);
		obj->setProperty("model", const_cast<char *>(model), static_cast<unsigned>(strlen(model)+1));
	}

	if (realDevice != acpiDevice) {
		DBGLOG("igfx", "user requested to fake with normal device-id");
		fakeDevice = acpiDevice;
	}

	if (fakeDevice) {
		if (fakeDevice != acpiDevice) {
			uint8_t bus = 0, dev = 0, fun = 0;
			WIOKit::getDeviceAddress(obj, bus, dev, fun);
			SYSLOG("igfx", "IGPU device (%02X:%02X.%02X) has device-id 0x%04X, you should change it to 0x%04X",
				   bus, dev, fun, acpiDevice, fakeDevice);
		}
		if (fakeDevice != realDevice) {
 			if (KernelPatcher::routeVirtual(obj, WIOKit::PCIConfigOffset::ConfigRead16, configRead16, &orgConfigRead16) &&
				KernelPatcher::routeVirtual(obj, WIOKit::PCIConfigOffset::ConfigRead32, configRead32, &orgConfigRead32))
				DBGLOG("igfx", "hooked configRead read methods!");
			else
				SYSLOG("igfx", "failed to hook configRead read methods!");
		}
	}

	// Framebuffer fixes
	uint32_t platform = getFramebufferId(obj, hasExternalAMD, hasExternalNVIDIA, true);
	if (platform != CPUInfo::DefaultInvalidPlatformId)
		connectorLessFrame = CPUInfo::isConnectorLessPlatformId(platform);
	else
		SYSLOG("igfx", "unsupported cpu generation has no frame id, this is likely an error!");

	// HDMI audio fixes
	if (!connectorLessFrame) {
		if (!obj->getProperty("hda-gfx"))
			obj->setProperty("hda-gfx", OSData::withBytes("onboard-1", sizeof("onboard-1")));
		else
			DBGLOG("igfx", "existing hda-gfx in IGPU");
	}

	// Other property fixes
	if (!obj->getProperty("built-in")) {
		DBGLOG("igfx", "fixing built-in in IGPU");
		uint8_t builtBytes[] { 0x01, 0x00, 0x00, 0x00 };
		obj->setProperty("built-in", OSData::withBytes(builtBytes, sizeof(builtBytes)));
	} else {
		DBGLOG("igfx", "found existing built-in in IGPU");
	}
}

void IGFX::loadIGScheduler4Patches(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	gKmGen9GuCBinary = patcher.solveSymbol<uint8_t *>(index, "__ZL17__KmGen9GuCBinary", address, size);
	if (gKmGen9GuCBinary) {
		DBGLOG("igfx", "obtained __KmGen9GuCBinary");
		patcher.clearError();

		auto loadGuC = patcher.solveSymbol(index, "__ZN13IGHardwareGuC13loadGuCBinaryEv", address, size);
		if (loadGuC) {
			DBGLOG("igfx", "obtained IGHardwareGuC::loadGuCBinary");
			patcher.clearError();

			// Lookup the assignment to the size register.
			uint8_t sizeReg[] {0x10, 0xC3, 0x00, 0x00};
			auto pos    = reinterpret_cast<uint8_t *>(loadGuC);
			auto endPos = pos + PAGE_SIZE;
			while (memcmp(pos, sizeReg, sizeof(sizeReg)) && pos < endPos)
				pos++;

			// Verify and store the size pointer
			if (pos != endPos) {
				pos += sizeof(uint32_t);
				firmwareSizePointer = reinterpret_cast<uint32_t *>(pos);
				DBGLOG("igfx", "discovered firmware size: %d bytes", *firmwareSizePointer);
				// Firmware size must not be bigger than 1 MB
				if ((*firmwareSizePointer & 0xFFFFF) == *firmwareSizePointer)
					// Firmware follows the signature
					signaturePointer[0] = gKmGen9GuCBinary + *firmwareSizePointer;
				else
					firmwareSizePointer = nullptr;
			}

			orgLoadGuCBinary = reinterpret_cast<t_load_guc_binary>(patcher.routeFunction(loadGuC, reinterpret_cast<mach_vm_address_t>(loadGuCBinary), true));
			if (patcher.getError() == KernelPatcher::Error::NoError)
				DBGLOG("igfx", "routed IGHardwareGuC::loadGuCBinary");
			else
				SYSLOG("igfx", "failed to route IGHardwareGuC::loadGuCBinary");
		} else {
			SYSLOG("igfx", "failed to resolve IGHardwareGuC::loadGuCBinary");
		}

		auto loadFW = patcher.solveSymbol(index, "__ZN12IGScheduler412loadFirmwareEv", address, size);
		if (loadFW) {
			DBGLOG("igfx", "obtained IGScheduler4::loadFirmware");
			patcher.clearError();

			orgLoadFirmware = reinterpret_cast<t_load_firmware>(patcher.routeFunction(loadFW, reinterpret_cast<mach_vm_address_t>(loadFirmware), true));
			if (patcher.getError() == KernelPatcher::Error::NoError)
				DBGLOG("igfx", "routed IGScheduler4::loadFirmware");
			else
				SYSLOG("igfx", "failed to route IGScheduler4::loadFirmware");
		} else {
			SYSLOG("igfx", "failed to resolve IGScheduler4::loadFirmware");
		}

		auto initSched = patcher.solveSymbol(index, "__ZN13IGHardwareGuC16initSchedControlEv", address, size);
		if (initSched) {
			DBGLOG("igfx", "obtained IGHardwareGuC::initSchedControl");
			patcher.clearError();
			orgInitSchedControl = reinterpret_cast<t_init_sched_control>(patcher.routeFunction(initSched, reinterpret_cast<mach_vm_address_t>(initSchedControl), true));
			if (patcher.getError() == KernelPatcher::Error::NoError)
				DBGLOG("igfx", "routed IGHardwareGuC::initSchedControl");
			else
				SYSLOG("igfx", "failed to route IGHardwareGuC::initSchedControl");
		} else {
			SYSLOG("igfx", "failed to resolve IGHardwareGuC::initSchedControl");
		}
	} else {
		SYSLOG("igfx", "failed to resoolve __KmGen9GuCBinary");
	}
}

void IGFX::loadIGGuCPatches(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	signaturePointer[0] = patcher.solveSymbol<uint8_t *>(index, "__ZL13__KmGuCBinary", address, size);
	if (signaturePointer[0]) {
		DBGLOG("igfx", "obtained __KmGuCBinary pointer");
	} else {
		SYSLOG("igfx", "failed to resolve __KmGuCBinary pointer");
		return;
	}

	signaturePointer[1] = patcher.solveSymbol<uint8_t *>(index, "__ZL13__KmHuCBinary", address, size);
	if (signaturePointer[1]) {
		DBGLOG("igfx", "obtained __KmHuCBinary pointer");
	} else {
		SYSLOG("igfx", "failed to resolve __KmHuCBinary pointer");
		return;
	}

	canUseSpringboard = patcher.solveSymbol<uint8_t *>(index, "__ZN5IGGuC18fCanUseSpringboardE", address, size);
	if (canUseSpringboard) {
		DBGLOG("igfx", "found IGGuC::fCanUseSpringboard");
	} else {
		canUseSpringboard = patcher.solveSymbol<uint8_t *>(index, "__ZN5IGGuC20m_bCanUseSpringboardE", address, size);
		if (canUseSpringboard) {
			DBGLOG("igfx", "found IGGuC::fCanUseSpringboard");
		} else {
			SYSLOG("igfx", "failed to find either springboard usage flag");
			return;
		}
	}

	orgSafeForceWake =  patcher.solveSymbol<t_safe_force_wake>(index, "__ZN16IntelAccelerator13SafeForceWakeEbj", address, size);
	if (orgSafeForceWake) {
		DBGLOG("igfx", "obtained IntelAccelerator::SafeForceWake");
	} else {
		SYSLOG("igfx", "failed to resolve IntelAccelerator::SafeForceWake");
		return;
	}

	auto loadGuC = patcher.solveSymbol(index, "__ZN5IGGuC10loadBinaryEb", address, size);
	if (loadGuC) {
		DBGLOG("igfx", "obtained IGGuC::loadBinary");
		patcher.clearError();
		orgLoadGuCBinary = reinterpret_cast<t_load_guc_binary>(patcher.routeFunction(loadGuC, reinterpret_cast<mach_vm_address_t>(loadGuCBinary), true));
		if (patcher.getError() == KernelPatcher::Error::NoError)
			DBGLOG("igfx", "routed IGGuC::loadBinary");
		else
			SYSLOG("igfx", "failed to route IGGuC::loadBinary");
	} else {
		SYSLOG("igfx", "failed to resolve IGGuC::loadBinary");
	}

	auto initSched = patcher.solveSymbol(index, "__ZN5IGGuC11initGucCtrlEPV9IGGucCtrl", address, size);
	if (initSched) {
		DBGLOG("igfx", "obtained IGGuC::initGucCtrl");
		patcher.clearError();
		orgInitSchedControl = reinterpret_cast<t_init_sched_control>(patcher.routeFunction(initSched, reinterpret_cast<mach_vm_address_t>(initSchedControl), true));
		if (patcher.getError() == KernelPatcher::Error::NoError)
			DBGLOG("igfx", "routed IGGuC::initGucCtrl");
		else
			SYSLOG("igfx", "failed to route IGGuC::initGucCtrl");
	} else {
		SYSLOG("igfx", "failed to resolve IGGuC::initGucCtrl");
	}

	auto initIntr = patcher.solveSymbol(index, "__ZN5IGGuC21initInterruptServicesEv", address, size);
	if (initIntr) {
		DBGLOG("igfx", "obtained IGGuC::initInterruptServices");
		patcher.clearError();
		orgInitInterruptServices = reinterpret_cast<t_init_intr_services>(patcher.routeFunction(initIntr, reinterpret_cast<mach_vm_address_t>(initInterruptServices), true));
		if (patcher.getError() == KernelPatcher::Error::NoError)
			DBGLOG("igfx", "routed IGGuC::initInterruptServices");
		else
			SYSLOG("igfx", "failed to route IGGuC::initInterruptServices");
	} else {
		SYSLOG("igfx", "failed to resolve IGGuC::initInterruptServices");
	}

	auto canLoad = patcher.solveSymbol(index, "__ZN5IGGuC15canLoadFirmwareEP22IOGraphicsAccelerator2", address, size);
	if (canLoad) {
		DBGLOG("igfx", "obtained IGGuC::canLoadFirmware");
		patcher.clearError();// mov eax,1
		// ret
		uint8_t ret[] {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};
		patcher.routeBlock(canLoad, ret, sizeof(ret));
		if (patcher.getError() != KernelPatcher::Error::NoError)
			SYSLOG("rad", "failed to patch IGGuC::canLoadFirmware");
		else
			DBGLOG("rad", "patched IGGuC::canLoadFirmware");
	} else {
		SYSLOG("igfx", "failed to resolve IGGuC::canLoadFirmware");
	}

	auto dmaMap = patcher.solveSymbol(index, "__ZN5IGGuC12dmaHostToGuCEyjjNS_12IGGucDmaTypeEb", address, size);
	if (dmaMap) {
		DBGLOG("igfx", "obtained IGGuC::dmaHostToGuC");
		patcher.clearError();
		orgDmaHostToGuC = reinterpret_cast<t_dma_host_to_guc>(patcher.routeFunction(dmaMap, reinterpret_cast<mach_vm_address_t>(dmaHostToGuC), true));
		if (patcher.getError() == KernelPatcher::Error::NoError)
			DBGLOG("igfx", "routed IGGuC::dmaHostToGuC");
		else
			SYSLOG("igfx", "failed to route IGGuC::dmaHostToGuC");
	} else {
		SYSLOG("igfx", "failed to resolve IGGuC::dmaHostToGuC");
	}
}

void IGFX::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	if (progressState != ProcessingState::EverythingDone) {
		for (size_t i = 0; i < kextListSize; i++) {
			if (kextList[i].loadIndex == index) {
				DBGLOG("igfx", "found %s (%d)", kextList[i].id, progressState);

				if (!(progressState & ProcessingState::CallbackPavpSessionRouted) &&
					(i == KextHD3000Graphics || i == KextHD4000Graphics || i == KextHD5000Graphics ||
					 i == KextBDWGraphics || i == KextSKLGraphics || i == KextKBLGraphics)) {
					mach_vm_address_t sessionCallback = 0;
					if (i == KextHD3000Graphics)
						sessionCallback = patcher.solveSymbol(index, "__ZN15Gen6Accelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb", address, size);
					else if (i == KextHD4000Graphics)
						sessionCallback = patcher.solveSymbol(index, "__ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb", address, size);
					else
						sessionCallback = patcher.solveSymbol(index, "__ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb", address, size);
					if (sessionCallback) {
						DBGLOG("igfx", "obtained PAVPCommandCallback");
						patcher.clearError();
						orgPavpSessionCallback = reinterpret_cast<t_pavp_session_callback>(patcher.routeFunction(sessionCallback, reinterpret_cast<mach_vm_address_t>(pavpSessionCallback), true));
						if (patcher.getError() == KernelPatcher::Error::NoError)
							DBGLOG("igfx", "routed PAVPCommandCallback");
						else
							SYSLOG("igfx", "failed to route PAVPCommandCallback");
					} else {
						SYSLOG("igfx", "failed to resolve PAVPCommandCallback");
					}
					progressState |= ProcessingState::CallbackPavpSessionRouted;
				}

				if (!(progressState & ProcessingState::CallbackFrameBufferInitRouted) && i == KextIOGraphicsFamily) {
					gIOFBVerboseBootPtr = patcher.solveSymbol<uint8_t *>(index, "__ZL16gIOFBVerboseBoot", address, size);
					if (gIOFBVerboseBootPtr) {
						DBGLOG("igfx", "obtained gIOFBVerboseBoot");
						auto ioFramebufferinit = patcher.solveSymbol(index, "__ZN13IOFramebuffer6initFBEv", address, size);
						if (ioFramebufferinit) {
							DBGLOG("igfx", "obtained IOFramebuffer::initFB");
							patcher.clearError();
							orgFrameBufferInit = reinterpret_cast<t_frame_buffer_init>(patcher.routeFunction(ioFramebufferinit, reinterpret_cast<mach_vm_address_t>(frameBufferInit), true));
							if (patcher.getError() == KernelPatcher::Error::NoError)
								DBGLOG("igfx", "routed IOFramebuffer::initFB");
							else
								SYSLOG("igfx", "failed to route IOFramebuffer::initFB");
						}
					} else {
						SYSLOG("igfx", "failed to resolve gIOFBVerboseBoot");
					}
					progressState |= ProcessingState::CallbackFrameBufferInitRouted;
				}

				if (!(progressState & ProcessingState::CallbackComputeLaneCountRouted) &&
					(i == KextSKLGraphicsFramebuffer || i == KextKBLGraphicsFramebuffer)) {
					auto compLane = patcher.solveSymbol(index, "__ZN31AppleIntelFramebufferController16ComputeLaneCountEPK29IODetailedTimingInformationV2jjPj", address, size);
					if (compLane) {
						DBGLOG("igfx", "obtained ComputeLaneCount");
						patcher.clearError();
						orgComputeLaneCount = reinterpret_cast<t_compute_lane_count>(patcher.routeFunction(compLane, reinterpret_cast<mach_vm_address_t>(computeLaneCount), true));
						if (patcher.getError() == KernelPatcher::Error::NoError)
							DBGLOG("igfx", "routed ComputeLaneCount");
						else
							SYSLOG("igfx", "failed to route ComputeLaneCount");
					} else {
						SYSLOG("igfx", "failed to resolve ComputeLaneCount");
					}

					progressState |= ProcessingState::CallbackComputeLaneCountRouted;
				}

				if (!(progressState & ProcessingState::CallbackDriverStartRouted) &&
					(i == KextHD3000Graphics || i == KextHD4000Graphics || i == KextHD5000Graphics ||
					 i == KextBDWGraphics || i == KextSKLGraphics || i == KextKBLGraphics)) {
					auto acceleratorStart = patcher.solveSymbol(index, i == KextHD3000Graphics ?
							"__ZN15Gen6Accelerator5startEP9IOService" : "__ZN16IntelAccelerator5startEP9IOService", address, size);
					if (acceleratorStart) {
						DBGLOG("igfx", "obtained Accelerator::start");
						patcher.clearError();
						orgGraphicsStart = reinterpret_cast<t_intel_graphics_start>(patcher.routeFunction(acceleratorStart, reinterpret_cast<mach_vm_address_t>(intelGraphicsStart), true));
						if (patcher.getError() == KernelPatcher::Error::NoError)
							DBGLOG("igfx", "routed Accelerator::start");
						else
							SYSLOG("igfx", "failed to route Accelerator::start");
					} else {
						SYSLOG("igfx", "failed to resolve Accelerator::start");
					}
					progressState |= ProcessingState::CallbackDriverStartRouted;
				}

				if (!(progressState & ProcessingState::CallbackGuCFirmwareUpdateRouted) && (i == KextSKLGraphics || i == KextKBLGraphics)) {
					if (decideLoadScheduler != BasicScheduler) {
						auto bufferWithOptions = patcher.solveSymbol(index, "__ZN20IGSharedMappedBuffer11withOptionsEP11IGAccelTaskmjj", address, size);
						if (bufferWithOptions) {
							DBGLOG("igfx", "obtained IGSharedMappedBuffer::withOptions");
							patcher.clearError();
							orgIgBufferWithOptions = reinterpret_cast<t_ig_buffer_with_options>(patcher.routeFunction(bufferWithOptions, reinterpret_cast<mach_vm_address_t>(igBufferWithOptions), true));
							if (patcher.getError() == KernelPatcher::Error::NoError)
								DBGLOG("igfx", "routed IGSharedMappedBuffer::withOptions");
							else
								SYSLOG("igfx", "failed to route IGSharedMappedBuffer::withOptions");
						} else {
							SYSLOG("igfx", "failed to resolve IGSharedMappedBuffer::withOptions");
						}

						auto getGpuVaddr = patcher.solveSymbol(index, "__ZNK14IGMappedBuffer20getGPUVirtualAddressEv", address, size);
						if (getGpuVaddr) {
							DBGLOG("igfx", "obtained IGMappedBuffer::getGPUVirtualAddress");
							patcher.clearError();
							orgIgGetGpuVirtualAddress = reinterpret_cast<t_ig_get_gpu_vaddr>(patcher.routeFunction(getGpuVaddr, reinterpret_cast<mach_vm_address_t>(igBufferGetGpuVirtualAddress), true));
							if (patcher.getError() == KernelPatcher::Error::NoError)
								DBGLOG("igfx", "routed IGMappedBuffer::getGPUVirtualAddress");
							else
								SYSLOG("igfx", "failed to route IGMappedBuffer::getGPUVirtualAddress");
						} else {
							SYSLOG("igfx", "failed to resolve IGMappedBuffer::getGPUVirtualAddress");
						}

						if (decideLoadScheduler == ReferenceScheduler)
							loadIGScheduler4Patches(patcher, index, address, size);
#ifdef IGFX_APPLE_SCHEDULER
						if (decideLoadScheduler == AppleScheduler || deviceLoadScheduler == AppleCustomScheduler)
							loadIGGuCPatches(patcher, index, address, size);
#endif
					}

					progressState |= ProcessingState::CallbackGuCFirmwareUpdateRouted;
				}
			}
		}
	}

	// Ignore all the errors for other processors
	patcher.clearError();
}
