//
//  kern_audio.cpp
//  IGFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/kern_iokit.hpp>
#include <Headers/kern_cpu.hpp>
#include <Headers/plugin_start.hpp>

#include "kern_audio.hpp"
#include "kern_igfx.hpp"

OSDefineMetaClassAndStructors(IntelGraphicsAudio, IOService)

uint32_t IntelGraphicsAudio::getAnalogLayout() {
	// For some DP monitors layout-id value should match HDEF layout-id
	// If we have HDEF properly configured, get the value
	static uint32_t layout = 0;

	if (!layout) {
		const char *tree[] {"AppleACPIPCI"};
		auto sect = WIOKit::findEntryByPrefix("/AppleACPIPlatformExpert", "PCI", gIOServicePlane);
		for (size_t i = 0; sect && i < arrsize(tree); i++) {
			sect = WIOKit::findEntryByPrefix(sect, tree[i], gIOServicePlane);
			if (sect && i+1 == arrsize(tree)) {
				auto iterator = sect->getChildIterator(gIOServicePlane);
				if (iterator) {
					IORegistryEntry *obj = nullptr;
					while ((obj = OSDynamicCast(IORegistryEntry, iterator->getNextObject())) != nullptr) {
						uint32_t vendor = 0;
						// We can technically check the class-code too, but it is not required.
						// HDEF and HDAU should have the same layout-id if any, so it is irrelevant which we find.
						if (WIOKit::getOSDataValue(obj, "layout-id", layout) &&
							WIOKit::getOSDataValue(obj, "vendor-id", vendor) &&
							vendor == WIOKit::VendorID::Intel) {
							DBGLOG("audio", "found intel audio %s with layout-id %u", safeString(obj->getName()), layout);
							return layout;
						}
					}
					iterator->release();
				}
			}
		}
		
		DBGLOG("audio", "failed to find HDEF layout-id, falling back to 1");
		layout = 0x1;
	}

	return layout;
}

IOService *IntelGraphicsAudio::probe(IOService *hdaService, SInt32 *score) {
	if (!ADDPR(startSuccess))
		return nullptr;

	if (!hdaService) {
		DBGLOG("audio", "received null digitial audio device");
		return nullptr;
	}

	IGFX::lockDeviceAccess();
	
	uint32_t hdaVen, hdaDev;
	if (!WIOKit::getOSDataValue(hdaService, "vendor-id", hdaVen) ||
		!WIOKit::getOSDataValue(hdaService, "device-id", hdaDev) ||
		hdaVen != WIOKit::VendorID::Intel) {
		SYSLOG("audio", "found an unsupported device");
		IGFX::unlockDeviceAccess();
		return nullptr;
	}
	
	auto hdaPlaneName = hdaService->getName();
	DBGLOG("audio", "corrects digital audio for hdau at %s with %04X:%04X",
		   safeString(hdaPlaneName), hdaVen, hdaDev);
	
	// HDEF device seems to always exist, so it will be labeled.
	// However, we will not touch mislabeled HDEF due to VoodooHDA.
	// AppleALC will rename HDEF properly soon (just need a higher probe score).
	bool isAppleAnalog = hdaPlaneName && !strcmp(hdaPlaneName, "HDEF");
	bool isMislabeledDigital = !hdaPlaneName || !strcmp(hdaPlaneName, "B0D3");
	bool isDigital = isMislabeledDigital || !strcmp(hdaPlaneName, "HDAU");

	if (!isDigital && !isAppleAnalog) {
		DBGLOG("audio", "found voodoo-like analog audio, ignoring.");
		IGFX::unlockDeviceAccess();
		return nullptr;
	}

	bool isConnectorLess = IGFX::isConnectorLessFrame();
	if (isDigital) {
		IGFX::correctGraphicsAudioProperties(hdaService, isConnectorLess, isMislabeledDigital);
	} else {
		// hda-gfx allows to separate the devices, it must be unique.
		// WhateverGreen and NvidiaGraphicsFixup use onboard-2 and so on for GFX.
		if (!isConnectorLess) {
			// Haswell and Broadwell have a dedicated device for digital audio source.
			auto gen = CPUInfo::getGeneration();
			if (gen != CPUInfo::CpuGeneration::Haswell && gen != CPUInfo::CpuGeneration::Broadwell) {
				if (!hdaService->getProperty("hda-gfx"))
					hdaService->setProperty("hda-gfx", OSData::withBytes("onboard-1", sizeof("onboard-1")));
				else
					DBGLOG("audio", "existing hda-gfx, assuming complete inject");
			}
		}

		// built-in is required for non-renamed devices
		if (!hdaService->getProperty("built-in")) {
			DBGLOG("audio", "fixing built-in in hdau");
			uint8_t builtBytes[] { 0x00 };
			hdaService->setProperty("built-in", builtBytes, sizeof(builtBytes));
		} else {
			DBGLOG("audio", "found existing built-in in hdau");
		}
	}

	IGFX::unlockDeviceAccess();

	return nullptr;
}
