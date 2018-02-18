//
//  kern_audio.cpp
//  IGFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/kern_iokit.hpp>
#include <Headers/plugin_start.hpp>

#include "kern_audio.hpp"

OSDefineMetaClassAndStructors(IntelGraphicsAudio, IOService)

uint32_t IntelGraphicsAudio::getAnalogLayout() {
	// For some DP monitors layout-id value should match HDEF layout-id
	// If we have HDEF properly configured, get the value
	static uint32_t layout = 0;
	
	if (!layout) {
		const char *tree[] {"AppleACPIPCI", "HDEF"};
		auto sect = WIOKit::findEntryByPrefix("/AppleACPIPlatformExpert", "PCI", gIOServicePlane);
		for (size_t i = 0; sect && i < arrsize(tree); i++) {
			sect = WIOKit::findEntryByPrefix(sect, tree[i], gIOServicePlane);
			if (sect && i+1 == arrsize(tree)) {
				if (WIOKit::getOSDataValue(sect, "layout-id", layout)) {
					DBGLOG("audio", "found HDEF with layout-id %u", layout);
					return layout;
				} else {
					SYSLOG("audio", "found HDEF with missing layout-id");
				}
			}
		}
		
		DBGLOG("audio", "failed to find HDEF layout-id, falling back to 1");
		layout = 0x1;
	}

	return layout;
}

IOService *IntelGraphicsAudio::probe(IOService *hdaService, SInt32 *score) {
	if (!ADDPR(startSuccess)) {
		return nullptr;
	}

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
		   hdaPlaneName ? hdaPlaneName : "(null)", hdaVen, hdaDev);
	
	if (hdaVen != 0x8086) {
		DBGLOG("audio", "unsupported hdau vendor");
		return nullptr;
	}

	// Do not mess with HDEF
	bool mislabeled = !hdaPlaneName || !strcmp(hdaPlaneName, "B0D3");
	if (!mislabeled && strcmp(hdaPlaneName, "HDAU")) {
		return nullptr;
	}
	
	// AppleHDAController only recognises HDEF and HDAU
	if (mislabeled) {
		DBGLOG("audio", "fixing audio plane name to HDAU");
		WIOKit::renameDevice(hdaService, "HDAU");
	}
	
	// hda-gfx allows to separate the devices, must be unique
	
	if (!hdaService->getProperty("hda-gfx")) {
		hdaService->setProperty("hda-gfx", OSData::withBytes("onboard-1", sizeof("onboard-1")));
	} else {
		DBGLOG("audio", "existing hda-gfx in hdau, assuming complete inject");
	}
	
	// layout-id is heard to be required in rare cases
	
	if (!hdaService->getProperty("layout-id")) {
		DBGLOG("audio", "fixing layout-id in hdau");
		uint32_t layout = getAnalogLayout();
		hdaService->setProperty("layout-id", OSData::withBytes(&layout, sizeof(layout)));
	} else {
		DBGLOG("audio", "found existing layout-id in hdau");
	}
	
	// built-in is required for non-renamed devices
	
	if (!hdaService->getProperty("built-in")) {
		DBGLOG("audio", "fixing built-in in hdau");
		uint8_t builtBytes[] { 0x01, 0x00, 0x00, 0x00 };
		hdaService->setProperty("built-in", OSData::withBytes(builtBytes, sizeof(builtBytes)));
	} else {
		DBGLOG("audio", "found existing built-in in hdau");
	}

	return nullptr;
}
