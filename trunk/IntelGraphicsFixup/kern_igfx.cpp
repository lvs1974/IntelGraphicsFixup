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
#include "kern_guc.hpp"
#include "kern_model.hpp"
#include "kern_regs.hpp"

// Only used in apple-driven callbacks
static IGFX *callbackIgfx = nullptr;
static KernelPatcher *callbackPatcher = nullptr;

static const char *kextHD3000Path[]          { "System/Library/Extensions/AppleIntelHD3000Graphics.kext/Contents/MacOS/AppleIntelHD3000Graphics" };
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

	error = lilu.onKextLoad(kextList, kextListSize, [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
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

bool IGFX::computeLaneCount(void *that, void *unk1, unsigned int bpp, int unk3, int *lane_count) {
	// unk3 is read in AppleIntelFramebufferController::GetDPCDInfo
	DBGLOG("igfx", "computeLaneCount: bpp = %d, unk3 = %d", bpp, unk3); // 24 0

	bool r = false;
	if (callbackIgfx) {
		// HD 530 reports to have 0 lanes max
		r = callbackIgfx->orgComputeLaneCount(that, unk1, bpp, unk3, lane_count);
		// We do not need this hack when we have no connectors
		if (!callbackIgfx->connectorLessFrame && !r && *lane_count == 0) {
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
			else if (callbackIgfx->decideLoadScheduler == AppleScheduler ||
					 callbackIgfx->decideLoadScheduler == AppleCustomScheduler)
				sched = 3; // force apple scheduler
			DBGLOG("igfx", "forcing scheduler preference %d", sched);
			newDev->setObject("GraphicsSchedulerSelect", OSNumber::withNumber(sched, 32));
			that->setProperty("Development", newDev);
		}
	}

	return callbackIgfx->orgGraphicsStart(that, provider);
}

bool IGFX::loadGuCBinary(void *that, bool flag) {
	bool r = false;
	if (callbackIgfx) {
		DBGLOG("igfx", "attempting to load firmware for %d scheduler for cpu gen %d",
			   callbackIgfx->decideLoadScheduler, callbackIgfx->cpuGeneration);

		// Go with for testing if requested.
		if (callbackIgfx->decideLoadScheduler == AppleCustomScheduler)
			return callbackIgfx->loadCustomBinary(that, flag);

		// Reset binary indexes
		callbackIgfx->currentBinaryIndex = -1;
		callbackIgfx->currentDmaIndex = -1;

		// This is required to skip the ME hash verification loop...
		bool shouldUnsetIONDrvMode = false;

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
		} else if (callbackIgfx->firmwareSizePointer) {
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

			auto v = &static_cast<ParamRegs *>(that)->params[0];
			DBGLOG("igfx", "fw params1 %08X %08X %08X %08X %08X", v[0], v[1], v[2], v[3], v[4]);
			DBGLOG("igfx", "fw params2 %08X %08X %08X %08X %08X", v[5], v[6], v[7], v[8], v[9]);
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

		if ((status & START_DMA) != START_DMA)
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

	auto igGuCBuffer = orgIgBufferWithOptions(fIGAccelTask, pageAlign(gucfwsize), 0x1A, 0);
	if (igGuCBuffer) {
		DBGLOG("igfx", "preparing GuC firmware %ld", gucfwsize);
		auto vaddr = static_cast<uint8_t **>(igGuCBuffer)[7];
		lilu_os_memcpy(vaddr, gucfw, gucfwsize);
		igGuCAddr = orgIgGetGpuVirtualAddress(igGuCBuffer);
	}

	auto igHuCBuffer = orgIgBufferWithOptions(fIGAccelTask, pageAlign(hucfwsize), 0x1A, 0);
	if (igHuCBuffer) {
		DBGLOG("igfx", "preparing HuC firmware %ld", hucfwsize);
		auto vaddr = static_cast<uint8_t **>(igHuCBuffer)[7];
		lilu_os_memcpy(vaddr, hucfw, hucfwsize);
		igHuCAddr = orgIgGetGpuVirtualAddress(igHuCBuffer);
	}

	auto igHuCSigBuffer = orgIgBufferWithOptions(fIGAccelTask, pageAlign(HuCFirmwareSignatureSize), 0x1A, 0);
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

	// Detect all the devices
	auto sect = WIOKit::findEntryByPrefix("/AppleACPIPlatformExpert", "PCI", gIOServicePlane);
	if (sect) sect = WIOKit::findEntryByPrefix(sect, "AppleACPIPCI", gIOServicePlane);
	if (sect) {
		bool foundIMEI = false, foundIGPU = false;
		auto iterator = sect->getChildIterator(gIOServicePlane);
		if (iterator) {
			IORegistryEntry *obj = nullptr;
			while ((obj = OSDynamicCast(IORegistryEntry, iterator->getNextObject())) != nullptr) {
				uint32_t vendor = 0, code = 0;
				if (WIOKit::getOSDataValue(obj, "vendor-id", vendor) &&
					WIOKit::getOSDataValue(obj, "class-code", code) &&
					vendor == WIOKit::VendorID::Intel) {
					const char *name = obj->getName();
					// VGA codes
					if (!foundIGPU && (code == WIOKit::ClassCode::DisplayController ||
									   code == WIOKit::ClassCode::VGAController)) {
						DBGLOG("igfx", "found Intel GPU device %s", name);
						if (!name || strcmp(name, "IGPU"))
							WIOKit::renameDevice(obj, "IGPU");

						uint32_t device = 0;
						if (!obj->getProperty("model") && WIOKit::getOSDataValue(obj, "device-id", device)) {
							auto model = getModelName(device);
							DBGLOG("igfx", "autodetect model name for IGPU %X gave %s", device, model ? model : "(null)");
							if (model)
								obj->setProperty("model", const_cast<char *>(model), static_cast<unsigned>(strlen(model)+1));
						}

						uint32_t platform = 0;
						if (WIOKit::getOSDataValue(obj, "AAPL,ig-platform-id", platform) ||
							WIOKit::getOSDataValue(obj, "AAPL,snb-platform-id", platform)) {
							connectorLessFrame = CPUInfo::isConnectorLessPlatformId(platform);
						} else {
							// Setting a default platform id instead of letting it to be fallen back to appears to improve boot speed for whatever reason.
							if (cpuGeneration == CPUInfo::CpuGeneration::Skylake)
								obj->setProperty("AAPL,ig-platform-id", OSData::withBytes(&CPUInfo::DefaultSkylakePlatformId, sizeof(uint32_t)));
							else if (cpuGeneration == CPUInfo::CpuGeneration::KabyLake)
								obj->setProperty("AAPL,ig-platform-id", OSData::withBytes(&CPUInfo::DefaultKabyLakePlatformId, sizeof(uint32_t)));
						}

						foundIGPU = true;
						continue;
					}

					if (code == WIOKit::ClassCode::PCIBridge) {
						DBGLOG("igfx", "found pci bridge %s", name ? name : "(unnamed)");
						auto gpuiterator = IORegistryIterator::iterateOver(obj, gIOServicePlane, kIORegistryIterateRecursively);
						if (gpuiterator) {
							IORegistryEntry *gpuobj = nullptr;
							while ((gpuobj = OSDynamicCast(IORegistryEntry, gpuiterator->getNextObject())) != nullptr) {
								uint32_t gpuvendor = 0, gpucode = 0;
								auto gpuname = gpuobj->getName();
								DBGLOG("igfx", "found %s on pci bridge", gpuname ? gpuname : "(unnamed)");
								if (WIOKit::getOSDataValue(gpuobj, "vendor-id", gpuvendor) &&
									WIOKit::getOSDataValue(gpuobj, "class-code", gpucode) &&
									(gpucode == WIOKit::ClassCode::DisplayController || gpucode == WIOKit::ClassCode::VGAController)) {
									if (gpuvendor == WIOKit::VendorID::ATIAMD) {
										DBGLOG("igfx", "found AMD GPU device %s", gpuname);
										hasExternalAMD = true;
										break;
									} else if (gpuvendor == WIOKit::VendorID::NVIDIA) {
										DBGLOG("igfx", "found NVIDIA GPU device %s", gpuname);
										hasExternalNVIDIA = true;
										break;
									}
								}
							}

							gpuiterator->release();
						}
					}

					if (!foundIMEI) {
						// Fortunately IMEI is always made by Intel
						bool correctName = name && !strcmp(name, "IMEI");
						if (correctName) {
							// IMEI is just right
							foundIMEI = true;
						} else if (!correctName && (code == 0x78000 || !strcmp(name, "HECI") || !strcmp(name, "MEI"))) {
							// IMEI is improperly named or unnamed! 0x78000 is implementation defined, but works on HSW so far.
							DBGLOG("igfx", "found invalid Intel ME device %s", name ? name : "(null)");
							WIOKit::renameDevice(obj, "IMEI");
							foundIMEI = true;
						}

						// We need to correct SNB IMEI device-id on 7-series motherboards.
						uint32_t device = 0;
						if (foundIMEI && cpuGeneration == CPUInfo::CpuGeneration::SandyBridge &&
							WIOKit::getOSDataValue(obj, "device-id", device) && device != 0x1c3a) {
							DBGLOG("igfx", "fixing Intel ME device id 0x%04X to 0x1c3a", device);
							device = 0x1c3a;
							obj->setProperty("device-id", &device, sizeof(device));
						}
					}
				}
			}
			iterator->release();
		}
	}
}

void IGFX::loadIGScheduler4Patches(KernelPatcher &patcher, size_t index) {
	gKmGen9GuCBinary = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZL17__KmGen9GuCBinary"));
	if (gKmGen9GuCBinary) {
		auto loadGuC = patcher.solveSymbol(index, "__ZN13IGHardwareGuC13loadGuCBinaryEv");
		if (loadGuC) {
			DBGLOG("igfx", "obtained __ZN13IGHardwareGuC13loadGuCBinaryEv");
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
				if ((*firmwareSizePointer & 0xFFFFF) == *firmwareSizePointer) {
					// Firmware follows the signature
					signaturePointer[0] = gKmGen9GuCBinary + *firmwareSizePointer;
				} else {
					firmwareSizePointer = nullptr;
				}
			}

			orgLoadGuCBinary = reinterpret_cast<t_load_guc_binary>(patcher.routeFunction(loadGuC, reinterpret_cast<mach_vm_address_t>(loadGuCBinary), true));
			if (patcher.getError() == KernelPatcher::Error::NoError) {
				DBGLOG("igfx", "routed __ZN13IGHardwareGuC13loadGuCBinaryEv");
			} else {
				SYSLOG("igfx", "failed to route __ZN13IGHardwareGuC13loadGuCBinaryEv");
			}
		} else {
			SYSLOG("igfx", "failed to resolve __ZN13IGHardwareGuC13loadGuCBinaryEv");
		}

		auto loadFW = patcher.solveSymbol(index, "__ZN12IGScheduler412loadFirmwareEv");
		if (loadFW) {
			DBGLOG("igfx", "obtained __ZN12IGScheduler412loadFirmwareEv");
			patcher.clearError();

			orgLoadFirmware = reinterpret_cast<t_load_firmware>(patcher.routeFunction(loadFW, reinterpret_cast<mach_vm_address_t>(loadFirmware), true));
			if (patcher.getError() == KernelPatcher::Error::NoError) {
				DBGLOG("igfx", "routed __ZN12IGScheduler412loadFirmwareEv");
			} else {
				SYSLOG("igfx", "failed to route __ZN12IGScheduler412loadFirmwareEv");
			}
		} else {
			SYSLOG("igfx", "failed to resolve __ZN12IGScheduler412loadFirmwareEv");
		}

		auto initSched = patcher.solveSymbol(index, "__ZN13IGHardwareGuC16initSchedControlEv");
		if (initSched) {
			DBGLOG("igfx", "obtained __ZN13IGHardwareGuC16initSchedControlEv");
			patcher.clearError();
			orgInitSchedControl = reinterpret_cast<t_init_sched_control>(patcher.routeFunction(initSched, reinterpret_cast<mach_vm_address_t>(initSchedControl), true));
			if (patcher.getError() == KernelPatcher::Error::NoError) {
				DBGLOG("igfx", "routed __ZN13IGHardwareGuC16initSchedControlEv");
			} else {
				SYSLOG("igfx", "failed to route __ZN13IGHardwareGuC16initSchedControlEv");
			}
		} else {
			SYSLOG("igfx", "failed to resolve __ZN13IGHardwareGuC16initSchedControlEv");
		}
	} else {
		SYSLOG("igfx", "failed to resoolve __ZL17__KmGen9GuCBinary");
	}
}

void IGFX::loadIGGuCPatches(KernelPatcher &patcher, size_t index) {
	signaturePointer[0] = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZL13__KmGuCBinary"));
	if (signaturePointer[0]) {
		DBGLOG("igfx", "obtained __KmGuCBinary pointer");
	} else {
		SYSLOG("igfx", "failed to resolve __KmGuCBinary pointer");
		return;
	}

	signaturePointer[1] = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZL13__KmHuCBinary"));
	if (signaturePointer[1]) {
		DBGLOG("igfx", "obtained __KmHuCBinary pointer");
	} else {
		SYSLOG("igfx", "failed to resolve __KmHuCBinary pointer");
		return;
	}

	canUseSpringboard = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZN5IGGuC18fCanUseSpringboardE"));
	if (canUseSpringboard) {
		DBGLOG("igfx", "found IGGuC::fCanUseSpringboard");
	} else {
		canUseSpringboard = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZN5IGGuC20m_bCanUseSpringboardE"));
		if (canUseSpringboard) {
			DBGLOG("igfx", "found IGGuC::fCanUseSpringboard");
		} else {
			SYSLOG("igfx", "failed to find either springboard usage flag");
			return;
		}
	}

	orgSafeForceWake =  reinterpret_cast<t_safe_force_wake>(patcher.solveSymbol(index, "__ZN16IntelAccelerator13SafeForceWakeEbj"));
	if (orgSafeForceWake) {
		DBGLOG("igfx", "obtained IntelAccelerator::SafeForceWake");
	} else {
		SYSLOG("igfx", "failed to resolve IntelAccelerator::SafeForceWake");
		return;
	}

	auto loadGuC = patcher.solveSymbol(index, "__ZN5IGGuC10loadBinaryEb");
	if (loadGuC) {
		DBGLOG("igfx", "obtained IGGuC::loadBinary");
		patcher.clearError();
		orgLoadGuCBinary = reinterpret_cast<t_load_guc_binary>(patcher.routeFunction(loadGuC, reinterpret_cast<mach_vm_address_t>(loadGuCBinary), true));
		if (patcher.getError() == KernelPatcher::Error::NoError) {
			DBGLOG("igfx", "routed IGGuC::loadBinary");
		} else {
			SYSLOG("igfx", "failed to route IGGuC::loadBinary");
		}
	} else {
		SYSLOG("igfx", "failed to resolve IGGuC::loadBinary");
	}

	auto initSched = patcher.solveSymbol(index, "__ZN5IGGuC11initGucCtrlEPV9IGGucCtrl");
	if (initSched) {
		DBGLOG("igfx", "obtained IGGuC::initGucCtrl");
		patcher.clearError();
		orgInitSchedControl = reinterpret_cast<t_init_sched_control>(patcher.routeFunction(initSched, reinterpret_cast<mach_vm_address_t>(initSchedControl), true));
		if (patcher.getError() == KernelPatcher::Error::NoError) {
			DBGLOG("igfx", "routed IGGuC::initGucCtrl");
		} else {
			SYSLOG("igfx", "failed to route IGGuC::initGucCtrl");
		}
	} else {
		SYSLOG("igfx", "failed to resolve IGGuC::initGucCtrl");
	}

	auto initIntr = patcher.solveSymbol(index, "__ZN5IGGuC21initInterruptServicesEv");
	if (initIntr) {
		DBGLOG("igfx", "obtained __ZN5IGGuC21initInterruptServicesEv");
		patcher.clearError();
		orgInitInterruptServices = reinterpret_cast<t_init_intr_services>(patcher.routeFunction(initIntr, reinterpret_cast<mach_vm_address_t>(initInterruptServices), true));
		if (patcher.getError() == KernelPatcher::Error::NoError) {
			DBGLOG("igfx", "routed IGGuC::initInterruptServices");
		} else {
			SYSLOG("igfx", "failed to route IGGuC::initInterruptServices");
		}
	} else {
		SYSLOG("igfx", "failed to resolve IGGuC::initInterruptServices");
	}

	auto canLoad = patcher.solveSymbol(index, "__ZN5IGGuC15canLoadFirmwareEP22IOGraphicsAccelerator2");
	if (canLoad) {
		// mov eax,1
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

	auto dmaMap = patcher.solveSymbol(index, "__ZN5IGGuC12dmaHostToGuCEyjjNS_12IGGucDmaTypeEb");
	if (dmaMap) {
		DBGLOG("igfx", "obtained __ZN5IGGuC12dmaHostToGuCEyjjNS_12IGGucDmaTypeEb");
		patcher.clearError();
		orgDmaHostToGuC = reinterpret_cast<t_dma_host_to_guc>(patcher.routeFunction(dmaMap, reinterpret_cast<mach_vm_address_t>(dmaHostToGuC), true));
		if (patcher.getError() == KernelPatcher::Error::NoError) {
			DBGLOG("igfx", "routed IGGuC::dmaHostToGuC");
		} else {
			SYSLOG("igfx", "failed to route IGGuC::dmaHostToGuC");
		}
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
						sessionCallback = patcher.solveSymbol(index, "__ZN15Gen6Accelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
					else if (i == KextHD4000Graphics)
						sessionCallback = patcher.solveSymbol(index, "__ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
					else
						sessionCallback = patcher.solveSymbol(index, "__ZN16IntelAccelerator19PAVPCommandCallbackE22PAVPSessionCommandID_tjPjb");
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
					gIOFBVerboseBootPtr = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZL16gIOFBVerboseBoot"));
					if (gIOFBVerboseBootPtr) {
						DBGLOG("igfx", "obtained gIOFBVerboseBoot");
						auto ioFramebufferinit = patcher.solveSymbol(index, "__ZN13IOFramebuffer6initFBEv");
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
					auto compLane = patcher.solveSymbol(index, "__ZN31AppleIntelFramebufferController16ComputeLaneCountEPK29IODetailedTimingInformationV2jjPj");
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
							"__ZN15Gen6Accelerator5startEP9IOService" : "__ZN16IntelAccelerator5startEP9IOService");
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
						auto bufferWithOptions = patcher.solveSymbol(index, "__ZN20IGSharedMappedBuffer11withOptionsEP11IGAccelTaskmjj");
						if (bufferWithOptions) {
							DBGLOG("igfx", "obtained __ZN20IGSharedMappedBuffer11withOptionsEP11IGAccelTaskmjj");
							patcher.clearError();
							orgIgBufferWithOptions = reinterpret_cast<t_ig_buffer_with_options>(patcher.routeFunction(bufferWithOptions, reinterpret_cast<mach_vm_address_t>(igBufferWithOptions), true));
							if (patcher.getError() == KernelPatcher::Error::NoError) {
								DBGLOG("igfx", "routed __ZN20IGSharedMappedBuffer11withOptionsEP11IGAccelTaskmjj");
							} else {
								SYSLOG("igfx", "failed to route __ZN20IGSharedMappedBuffer11withOptionsEP11IGAccelTaskmjj");
							}
						} else {
							SYSLOG("igfx", "failed to resolve __ZN20IGSharedMappedBuffer11withOptionsEP11IGAccelTaskmjj");
						}

						auto getGpuVaddr = patcher.solveSymbol(index, "__ZNK14IGMappedBuffer20getGPUVirtualAddressEv");
						if (getGpuVaddr) {
							DBGLOG("igfx", "obtained __ZNK14IGMappedBuffer20getGPUVirtualAddressEv");
							patcher.clearError();
							orgIgGetGpuVirtualAddress = reinterpret_cast<t_ig_get_gpu_vaddr>(patcher.routeFunction(getGpuVaddr, reinterpret_cast<mach_vm_address_t>(igBufferGetGpuVirtualAddress), true));
							if (patcher.getError() == KernelPatcher::Error::NoError) {
								DBGLOG("igfx", "routed __ZNK14IGMappedBuffer20getGPUVirtualAddressEv");
							} else {
								SYSLOG("igfx", "failed to route __ZNK14IGMappedBuffer20getGPUVirtualAddressEv");
							}
						} else {
							SYSLOG("igfx", "failed to resolve __ZNK14IGMappedBuffer20getGPUVirtualAddressEv");
						}

						if (decideLoadScheduler == ReferenceScheduler)
							loadIGScheduler4Patches(patcher, index);
						else
							loadIGGuCPatches(patcher, index);
					}

					progressState |= ProcessingState::CallbackGuCFirmwareUpdateRouted;
				}
			}
		}
	}

	// Ignore all the errors for other processors
	patcher.clearError();
}
