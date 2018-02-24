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

// Only used in apple-driven callbacks
static IGFX *callbackIgfx = nullptr;
static KernelPatcher *callbackPatcher = nullptr;

static const char *kextHD3000Path[]          { "System/Library/Extensions/AppleIntelHD3000Graphics.kext/Contents/MacOS/AppleIntelHD3000Graphics" };
static const char *kextHD4000Path[]          { "/System/Library/Extensions/AppleIntelHD4000Graphics.kext/Contents/MacOS/AppleIntelHD4000Graphics" };
static const char *kextHD5000Path[]          { "/System/Library/Extensions/AppleIntelHD5000Graphics.kext/Contents/MacOS/AppleIntelHD5000Graphics" };
static const char *kextSKLPath[]             { "/System/Library/Extensions/AppleIntelSKLGraphics.kext/Contents/MacOS/AppleIntelSKLGraphics" };
static const char *kextSKLFramebufferPath[]  { "/System/Library/Extensions/AppleIntelSKLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelSKLGraphicsFramebuffer" };
static const char *kextKBLPath[]             { "/System/Library/Extensions/AppleIntelKBLGraphics.kext/Contents/MacOS/AppleIntelKBLGraphics" };
static const char *kextKBLFramebufferPath[]  { "/System/Library/Extensions/AppleIntelKBLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelKBLGraphicsFramebuffer" };
static const char *kextIOGraphicsPath[]      { "/System/Library/Extensions/IOGraphicsFamily.kext/IOGraphicsFamily" };

static KernelPatcher::KextInfo kextList[] {
	{ "com.apple.driver.AppleIntelHD3000Graphics",         kextHD3000Path,         1, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelHD4000Graphics",         kextHD4000Path,         1, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelHD5000Graphics",         kextHD5000Path,         1, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelSKLGraphics",            kextSKLPath,            1, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelSKLGraphicsFramebuffer", kextSKLFramebufferPath, 1, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelKBLGraphics",            kextKBLPath,            1, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.driver.AppleIntelKBLGraphicsFramebuffer", kextKBLFramebufferPath, 1, {}, {}, KernelPatcher::KextInfo::Unloaded },
	{ "com.apple.iokit.IOGraphicsFamily",                  kextIOGraphicsPath,     1, {true}, {}, KernelPatcher::KextInfo::Unloaded },
};

enum : size_t {
	KextHD3000Graphics,
	KextHD4000Graphics,
	KextHD5000Graphics,
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

	int tmp;
	// Allow GuC firmware patches to be disabled
	if (getKernelVersion() < KernelVersion::HighSierra && decideLoadScheduler == 1) {
		SYSLOG("igfx", "IGScheduler4 unsupported before 10.13, disabling GuC!");
		decideLoadScheduler = BasicScheduler;
	} else if (PE_parse_boot_argn("-disablegfxfirmware", &tmp, sizeof(tmp))) {
		SYSLOG("igfx", "-disablegfxfirmware flag may negatively affect IGPU performance!");
		decideLoadScheduler = BasicScheduler;
	} else if (decideLoadScheduler >= TotalSchedulers) {
		SYSLOG("igfx", "invalid igfxfw option, disabling GuC!");
		decideLoadScheduler = BasicScheduler;
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
		// For AMD we do a zero-fill.
		bool zeroFill  = tryBackCopy && callbackIgfx->connectorLessFrame;
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
	// There is an option to load a generic firmware, which we set here.
	auto dev = OSDynamicCast(OSDictionary, that->getProperty("Development"));
	if (dev && dev->getObject("GraphicsSchedulerSelect")) {
		auto newDev = OSDynamicCast(OSDictionary, dev->copyCollection());
		if (newDev) {
			uint32_t sched = 2; // force disable via plist
			if (callbackIgfx->decideLoadScheduler == ReferenceScheduler)
				sched = 4; // force reference scheduler
			else if (callbackIgfx->decideLoadScheduler == AppleScheduler)
				sched = 3; // force apple scheduler
			DBGLOG("igfx", "forcing scheduler preference %d", sched);
			newDev->setObject("GraphicsSchedulerSelect", OSNumber::withNumber(sched, 32));
			that->setProperty("Development", newDev);
		}
	}

	return callbackIgfx->orgGraphicsStart(that, provider);
}

bool IGFX::canLoadFirmware(void *that, void *accelerator) {
	if (callbackIgfx) {
		DBGLOG("igfx", "canLoadFirmware request with scheduler %d and sb ptr %d",
			   callbackIgfx->decideLoadScheduler, callbackIgfx->canUseSpringboard ? 1 : 0);
		// Ensure Apple scheduler is never loaded (on 10.12 KBL too) unless asked explicitly.
		if (callbackIgfx->decideLoadScheduler == AppleScheduler)
			return true;
	}

	return false;
}

bool IGFX::loadGuCBinary(void *that, bool flag) {
	bool r = false;
	if (callbackIgfx) {
		DBGLOG("igfx", "attempting to load firmware for %d scheduler for cpu gen %d",
			   callbackIgfx->decideLoadScheduler, callbackIgfx->cpuGeneration);

		// Reset binary indexes
		callbackIgfx->currentBinaryIndex = -1;
		callbackIgfx->currentDmaIndex = -1;

		// This is required to skip the ME hash verification loop...
		bool shouldUnsetIONDrvMode = false;

		// firmwareSizePointer is only required for IGScheduler4
		if (callbackIgfx->decideLoadScheduler == AppleScheduler) {
			auto intelAccelerator = static_cast<uint8_t **>(that)[2];
			if (!(intelAccelerator[4096] & 0x10)) {
				DBGLOG("igfx", "overwriting intelAccelerator feature bit 0x10 in 0x%02X", intelAccelerator[4096]);
				intelAccelerator[4096] |= 0x10;
				shouldUnsetIONDrvMode = true;
			}
			// Ensure Apple springboard is disabled
			if (callbackIgfx->canUseSpringboard) {
				DBGLOG("igfx", "making sure springboard will not be loaded");
				*callbackIgfx->canUseSpringboard = false;
			}

			callbackIgfx->performingFirmwareLoad = true;
		} else if (callbackIgfx->firmwareSizePointer) {
			callbackIgfx->performingFirmwareLoad = true;
		}

		r = callbackIgfx->orgLoadGuCBinary(that, flag);
		DBGLOG("igfx", "loadGuCBinary returned %d", r);

		// Currently AppleScheduler does not work, for some reason after loading HuC and GuC firmwares
		// we receive 0x71ec in GUC_STATUS (0xC000) register, which unfortunately is not documented:
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

void *IGFX::igBufferGetGpuVirtualAddress(void *that) {
	void *r = nullptr;
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
		}
		r = callbackIgfx->orgIgGetGpuVirtualAddress(that);
	}
	return r;
}

bool IGFX::dmaHostToGuC(void *that, uint64_t gpuAddr, uint32_t gpuReg, uint32_t dataLen, uint32_t dmaType, bool unk) {
	bool r = false;
	if (callbackIgfx) {
		callbackIgfx->currentDmaIndex++;
		DBGLOG("igfx", "dma host -> guc to reg 0x%04X with size %04X dma %d unk %d dma index %d",
			   gpuReg, dataLen, dmaType, unk, callbackIgfx->currentDmaIndex);
		if (callbackIgfx->currentDmaIndex == 0 && callbackIgfx->realBinarySize[1] > 0) {
			dataLen = callbackIgfx->realBinarySize[1];
			DBGLOG("igfx", "dma host -> guc replaced HuC size with %04X", dataLen);
		} else if (callbackIgfx->currentDmaIndex == 1 && callbackIgfx->realBinarySize[0] > 0) {
			dataLen = callbackIgfx->realBinarySize[0];
			DBGLOG("igfx", "dma host -> guc replaced GuC size with %04X", dataLen);
		}

		r = callbackIgfx->orgDmaHostToGuC(that, gpuAddr, gpuReg, dataLen, dmaType, unk);
		DBGLOG("igfx", "dma host -> guc returned %d", r);
	}

	return r;
}

void IGFX::processKernel(KernelPatcher &patcher) {
	cpuGeneration = CPUInfo::getGeneration();

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

	auto sect = WIOKit::findEntryByPrefix("/AppleACPIPlatformExpert", "PCI", gIOServicePlane);
	if (sect) sect = WIOKit::findEntryByPrefix(sect, "AppleACPIPCI", gIOServicePlane);
	if (sect) {
		bool foundIMEI = false, foundIGPU = false;
		auto iterator = sect->getChildIterator(gIOServicePlane);
		if (iterator) {
			IORegistryEntry *obj = nullptr;
			while ((obj = OSDynamicCast(IORegistryEntry, iterator->getNextObject())) != nullptr) {
				uint32_t vendor = 0, code = 0;
				if (WIOKit::getOSDataValue(obj, "vendor-id", vendor) && vendor == 0x8086 &&
					WIOKit::getOSDataValue(obj, "class-code", code)) {
					const char *name = obj->getName();
					if (!foundIGPU && (code == 0x38000 || code == 0x30000)) {
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
						if (WIOKit::getOSDataValue(obj, "AAPL,ig-platform-id", platform)) {
							connectorLessFrame = CPUInfo::isConnectorLessPlatformId(platform);
						} else {
							// Setting a default platform id instead of letting it to be fallen back to appears to improve boot speed for whatever reason.
							if (cpuGeneration == CPUInfo::CpuGeneration::Skylake)
								obj->setProperty("AAPL,ig-platform-id", OSData::withBytes(&CPUInfo::DefaultSkylakePlatformId, sizeof(uint32_t)));
							else if (cpuGeneration == CPUInfo::CpuGeneration::KabyLake)
								obj->setProperty("AAPL,ig-platform-id", OSData::withBytes(&CPUInfo::DefaultKabyLakePlatformId, sizeof(uint32_t)));
						}

						foundIGPU = true;
					} else if (!foundIMEI) {
						bool correctName = name && !strcmp(name, "IMEI");
						if (correctName) {
							// IMEI is just right
							foundIMEI = true;
						} else if (!correctName && (code == 0x78000 || !strcmp(name, "HECI") || !strcmp(name, "MEI"))) {
							// IMEI is improperly named or unnamed!
							DBGLOG("igfx", "found invalid Intel ME device %s", name ? name : "(null)");
							WIOKit::renameDevice(obj, "IMEI");
							foundIMEI = true;
						}
					}
				}
				if (foundIMEI && foundIGPU)
					break;
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
		DBGLOG("igfx", "obtained __ZL13__KmGuCBinary pointer");
	} else {
		SYSLOG("igfx", "failed to resolve __ZL13__KmGuCBinary pointer");
		return;
	}

	signaturePointer[1] = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZL13__KmHuCBinary"));
	if (signaturePointer[1]) {
		DBGLOG("igfx", "obtained __ZL13__KmHuCBinary pointer");
	} else {
		SYSLOG("igfx", "failed to resolve __ZL13__KmHuCBinary pointer");
		return;
	}

	auto loadGuC = patcher.solveSymbol(index, "__ZN5IGGuC10loadBinaryEb");
	if (loadGuC) {
		DBGLOG("igfx", "obtained __ZN5IGGuC10loadBinaryEb");
		patcher.clearError();
		orgLoadGuCBinary = reinterpret_cast<t_load_guc_binary>(patcher.routeFunction(loadGuC, reinterpret_cast<mach_vm_address_t>(loadGuCBinary), true));
		if (patcher.getError() == KernelPatcher::Error::NoError) {
			DBGLOG("igfx", "routed __ZN5IGGuC10loadBinaryEb");
		} else {
			SYSLOG("igfx", "failed to route __ZN5IGGuC10loadBinaryEb");
		}
	} else {
		SYSLOG("igfx", "failed to resolve __ZN5IGGuC10loadBinaryEb");
	}

	auto initSched = patcher.solveSymbol(index, "__ZN5IGGuC11initGucCtrlEPV9IGGucCtrl");
	if (initSched) {
		DBGLOG("igfx", "obtained __ZN5IGGuC11initGucCtrlEPV9IGGucCtrl");
		patcher.clearError();
		orgInitSchedControl = reinterpret_cast<t_init_sched_control>(patcher.routeFunction(initSched, reinterpret_cast<mach_vm_address_t>(initSchedControl), true));
		if (patcher.getError() == KernelPatcher::Error::NoError) {
			DBGLOG("igfx", "routed __ZN5IGGuC11initGucCtrlEPV9IGGucCtrl");
		} else {
			SYSLOG("igfx", "failed to route __ZN5IGGuC11initGucCtrlEPV9IGGucCtrl");
		}
	} else {
		SYSLOG("igfx", "failed to resolve __ZN5IGGuC11initGucCtrlEPV9IGGucCtrl");
	}

	auto dmaMap = patcher.solveSymbol(index, "__ZN5IGGuC12dmaHostToGuCEyjjNS_12IGGucDmaTypeEb");
	if (dmaMap) {
		DBGLOG("igfx", "obtained __ZN5IGGuC12dmaHostToGuCEyjjNS_12IGGucDmaTypeEb");
		patcher.clearError();
		orgDmaHostToGuC = reinterpret_cast<t_dma_host_to_guc>(patcher.routeFunction(dmaMap, reinterpret_cast<mach_vm_address_t>(dmaHostToGuC), true));
		if (patcher.getError() == KernelPatcher::Error::NoError) {
			DBGLOG("igfx", "routed __ZN5IGGuC12dmaHostToGuCEyjjNS_12IGGucDmaTypeEb");
		} else {
			SYSLOG("igfx", "failed to route __ZN5IGGuC12dmaHostToGuCEyjjNS_12IGGucDmaTypeEb");
		}
	} else {
		SYSLOG("igfx", "failed to resolve __ZN5IGGuC12dmaHostToGuCEyjjNS_12IGGucDmaTypeEb");
	}
}

void IGFX::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	if (progressState != ProcessingState::EverythingDone) {
		for (size_t i = 0; i < kextListSize; i++) {
			if (kextList[i].loadIndex == index) {
				DBGLOG("igfx", "found %s (%d)", kextList[i].id, progressState);

				if (!(progressState & ProcessingState::CallbackPavpSessionRouted) &&
					(i == KextHD5000Graphics || i == KextSKLGraphics || i == KextKBLGraphics)) {
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

				if (!(progressState & ProcessingState::CallbackPavpSessionHD3000Routed) && i == KextHD3000Graphics) {
					auto sessionCallbackHD3000 = patcher.solveSymbol(index, "__ZN15Gen6Accelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
					if (sessionCallbackHD3000) {
						DBGLOG("igfx", "obtained __ZN15Gen6Accelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
						patcher.clearError();
						orgPavpSessionCallback = reinterpret_cast<t_pavp_session_callback>(patcher.routeFunction(sessionCallbackHD3000, reinterpret_cast<mach_vm_address_t>(pavpSessionCallback), true));
						if (patcher.getError() == KernelPatcher::Error::NoError) {
							DBGLOG("igfx", "routed __ZN15Gen6Accelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
							progressState |= ProcessingState::CallbackPavpSessionHD3000Routed;
						} else {
							SYSLOG("igfx", "failed to route __ZN15Gen6Accelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
						}
					} else {
						SYSLOG("igfx", "failed to resolve __ZN15Gen6Accelerator19PAVPCommandCallbackE22PAVPSessionCommandID_t18PAVPSessionAppID_tPjb");
					}
				}

				if (!(progressState & ProcessingState::CallbackPavpSessionHD4000Routed) && i == KextHD4000Graphics) {
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

				if (!(progressState & ProcessingState::CallbackFrameBufferInitRouted) && i == KextIOGraphicsFamily) {
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

				if (!(progressState & ProcessingState::CallbackComputeLaneCountRouted) && (i == KextSKLGraphicsFramebuffer || i == KextKBLGraphicsFramebuffer)) {
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

				if (!(progressState & ProcessingState::CallbackDriverStartRouted) &&
					(i == KextHD3000Graphics || i == KextHD4000Graphics || i == KextHD5000Graphics || i == KextSKLGraphics || i == KextKBLGraphics)) {

					auto acceleratorStart = patcher.solveSymbol(index, "__ZN16IntelAccelerator5startEP9IOService");
					if (acceleratorStart) {
						DBGLOG("igfx", "obtained IntelAccelerator::start");
						patcher.clearError();
						orgGraphicsStart = reinterpret_cast<t_intel_graphics_start>(patcher.routeFunction(acceleratorStart, reinterpret_cast<mach_vm_address_t>(intelGraphicsStart), true));
						if (patcher.getError() == KernelPatcher::Error::NoError) {
							DBGLOG("igfx", "routed IntelAccelerator::start");
							progressState |= ProcessingState::CallbackDriverStartRouted;
						} else {
							SYSLOG("igfx", "failed to route IntelAccelerator::start");
						}
					} else {
						SYSLOG("igfx", "failed to resolve IntelAccelerator::start");
					}
				}

				if (!(progressState & ProcessingState::CallbackGuCFirmwareUpdateRouted) && (i == KextSKLGraphics || i == KextKBLGraphics)) {
					canUseSpringboard = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZN5IGGuC18fCanUseSpringboardE"));
					if (canUseSpringboard) {
						DBGLOG("igfx", "found __ZN5IGGuC18fCanUseSpringboardE");
					} else {
						canUseSpringboard = reinterpret_cast<uint8_t *>(patcher.solveSymbol(index, "__ZN5IGGuC20m_bCanUseSpringboardE"));
						if (canUseSpringboard) {
							DBGLOG("igfx", "found __ZN5IGGuC20m_bCanUseSpringboardE");
						} else {
							SYSLOG("igfx", "failed to find either springboard usage flag");
						}
					}

					auto canLoad = patcher.solveSymbol(index, "__ZN5IGGuC15canLoadFirmwareEP22IOGraphicsAccelerator2");
					if (canLoad) {
						DBGLOG("igfx", "obtained __ZN5IGGuC15canLoadFirmwareEP22IOGraphicsAccelerator2");
						patcher.clearError();
						patcher.routeFunction(canLoad, reinterpret_cast<mach_vm_address_t>(canLoadFirmware));
						if (patcher.getError() == KernelPatcher::Error::NoError) {
							DBGLOG("igfx", "routed __ZN5IGGuC15canLoadFirmwareEP22IOGraphicsAccelerator2");
						} else {
							SYSLOG("igfx", "failed to route __ZN5IGGuC15canLoadFirmwareEP22IOGraphicsAccelerator2");
						}
					} else {
						SYSLOG("igfx", "failed to resolve __ZN5IGGuC15canLoadFirmwareEP22IOGraphicsAccelerator2");
					}

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
