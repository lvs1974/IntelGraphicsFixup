//
//  kern_start.cpp
//  IntelGraphicsFixup
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>

#include "kern_igfx.hpp"

static IGFX igfx;

static const char *bootargOff[] {
	"-igfxoff"
};

static const char *bootargDebug[] {
	"-igfxdbg"
};

static const char *bootargBeta[] {
	"-igfxbeta"
};

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
	bootargOff,
	sizeof(bootargOff)/sizeof(bootargOff[0]),
	bootargDebug,
	sizeof(bootargDebug)/sizeof(bootargDebug[0]),
	bootargBeta,
	sizeof(bootargBeta)/sizeof(bootargBeta[0]),
	KernelVersion::MountainLion,
	KernelVersion::HighSierra,
	[]() {
		igfx.init();
	}
};





