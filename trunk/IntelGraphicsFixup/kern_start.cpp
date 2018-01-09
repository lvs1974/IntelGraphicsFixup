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
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
	bootargOff,
	arrsize(bootargOff),
	bootargDebug,
	arrsize(bootargDebug),
	bootargBeta,
	arrsize(bootargBeta),
	KernelVersion::MountainLion,
	KernelVersion::HighSierra,
	[]() {
		igfx.init();
	}
};





