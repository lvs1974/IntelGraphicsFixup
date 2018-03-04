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
	
	uint32_t hdaVen, hdaDev;
	if (!WIOKit::getOSDataValue(hdaService, "vendor-id", hdaVen) ||
		!WIOKit::getOSDataValue(hdaService, "device-id", hdaDev)) {
		SYSLOG("audio", "found an unknown device");
		return nullptr;
	}
	
	auto hdaPlaneName = hdaService->getName();
	DBGLOG("audio", "corrects digital audio for hdau at %s with %04X:%04X",
		   safeString(hdaPlaneName), hdaVen, hdaDev);
	
	if (hdaVen != WIOKit::VendorID::Intel) {
		DBGLOG("audio", "unsupported hdau vendor");
		return nullptr;
	}

	// HDEF device seems to always exist, so it will be labeled.
	// However, we will not touch mislabeled HDEF due to VoodooHDA.
	// AppleALC will rename HDEF properly soon (just need a higher probe score).
	bool isAppleAnalog = hdaPlaneName && !strcmp(hdaPlaneName, "HDEF");
	bool isMislabeledDigital = !hdaPlaneName || !strcmp(hdaPlaneName, "B0D3");
	bool isDigital = isMislabeledDigital || !strcmp(hdaPlaneName, "HDAU");

	if (!isDigital && !isAppleAnalog) {
		DBGLOG("audio", "found voodoo-like analog audio, ignoring.");
		return nullptr;
	}

	bool isConnectorLess = IGFX::isConnectorLessFrame();

	// There is no reason to spend power on HDAU when IGPU has no connectors
	if (isConnectorLess && isDigital) {
		auto pci = OSDynamicCast(IOService, hdaService->getParentEntry(gIOServicePlane));
		if (pci) {
			DBGLOG("igfx", "received digital audio parent %s", safeString(pci->getName()));
			hdaService->stop(pci);
			bool success = hdaService->terminate();
			DBGLOG("igfx", "terminating digital audio %s (code %d)",
				   safeString(hdaPlaneName), success);
			// Only return after successful termination.
			// Otherwise at least try to rename stuff.
			if (success)
				return nullptr;
		} else {
			SYSLOG("igfx", "failed to find digital audio parent for termination");
		}
	}

	// AppleHDAController only recognises HDEF and HDAU.
	if (isMislabeledDigital) {
		DBGLOG("audio", "fixing audio plane name to HDAU");
		WIOKit::renameDevice(hdaService, "HDAU");
	}

	// hda-gfx allows to separate the devices, must be unique
	// WhateverGreen and NvidiaGraphicsFixup use onboard-2 and so on for GFX.
	if (!isConnectorLess) {
		if (!hdaService->getProperty("hda-gfx")) {
			// Haswell and Broadwell have a dedicated device for digital audio source.
			auto gen = CPUInfo::getGeneration();
			if (isDigital || (gen != CPUInfo::CpuGeneration::Haswell && gen != CPUInfo::CpuGeneration::Broadwell))
				hdaService->setProperty("hda-gfx", OSData::withBytes("onboard-1", sizeof("onboard-1")));
		} else {
			DBGLOG("audio", "existing hda-gfx, assuming complete inject");
		}

		// layout-id is heard to be required in rare cases
		if (!isAppleAnalog) {
			if (!hdaService->getProperty("layout-id")) {
				DBGLOG("audio", "fixing layout-id in hdau");
				uint32_t layout = getAnalogLayout();
				hdaService->setProperty("layout-id", &layout, sizeof(layout));
			} else {
				DBGLOG("audio", "found existing layout-id in hdau");
			}
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

	return nullptr;
}
