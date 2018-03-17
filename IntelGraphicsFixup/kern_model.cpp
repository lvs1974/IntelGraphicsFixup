//
//  kern_model.cpp
//  IntelGraphicsFixup
//
//  Copyright © 2018 lvs1974. All rights reserved.
//

#include <Headers/kern_util.hpp>

#include "kern_model.hpp"

struct DeviceModel {
	uint32_t device;
	uint32_t fake;
	const char *name;
};

static DeviceModel deviceModels[] {
	// For Sandy only 0x0116 and 0x0126 controllers are properly supported by AppleIntelSNBGraphicsFB.
	// 0x0102 and 0x0106 are implemented as AppleIntelSNBGraphicsController/AppleIntelSNBGraphicsController2.
	// AppleIntelHD3000Graphics actually supports more (0x0106, 0x0601, 0x0102, 0x0116, 0x0126).
	// To make sure we have at least acceleration we fake unsupported ones as 0x0102.
	// 0x0106 is likely a typo from 0x0106 or a fulty device (AppleIntelHD3000Graphics)
	{ 0x0106, 0x0000, "Intel HD Graphics 2000" },
	{ 0x0601, 0x0106, "Intel HD Graphics 2000" },
	{ 0x0102, 0x0000, "Intel HD Graphics 2000" },
	{ 0x0112, 0x0116, "Intel HD Graphics 2000" },
	{ 0x0116, 0x0000, "Intel HD Graphics 3000" },
	{ 0x0122, 0x0126, "Intel HD Graphics 2000" },
	{ 0x0126, 0x0000, "Intel HD Graphics 3000" },
	{ 0x0152, 0x0000, "Intel HD Graphics 2500" },
	{ 0x015A, 0x0152, "Intel HD Graphics P2500" },
	{ 0x0156, 0x0000, "Intel HD Graphics 2500" },
	{ 0x0162, 0x0000, "Intel HD Graphics 4000" },
	{ 0x016A, 0x0162, "Intel HD Graphics P4000" },
	{ 0x0166, 0x0000, "Intel HD Graphics 4000" },
	{ 0x0D26, 0x0000, "Intel Iris Pro Graphics 5200" },
	{ 0x0D22, 0x0000, "Intel Iris Pro Graphics 5200" },
	{ 0x0D2A, 0x0000, "Intel Iris Pro Graphics 5200" },
	{ 0x0D2B, 0x0000, "Intel Iris Pro Graphics 5200" },
	{ 0x0D2E, 0x0000, "Intel Iris Pro Graphics 5200" },
	{ 0x0A26, 0x0000, "Intel HD Graphics 5000" },
	{ 0x0A2A, 0x0A2E, "Intel Iris Graphics 5100" },
	{ 0x0A2B, 0x0A2E, "Intel Iris Graphics 5100" },
	{ 0x0A2E, 0x0000, "Intel Iris Graphics 5100" },
	{ 0x0412, 0x0000, "Intel HD Graphics 4600" },
	{ 0x0416, 0x0412, "Intel HD Graphics 4600" },
	{ 0x041A, 0x0412, "Intel HD Graphics P4600" },
	{ 0x041B, 0x0412, nullptr },
	{ 0x041E, 0x0412, "Intel HD Graphics 4400" },
	{ 0x0A12, 0x0412, nullptr },
	{ 0x0A16, 0x0412, "Intel HD Graphics 4400" },
	{ 0x0A1A, 0x0412, nullptr },
	{ 0x0A1E, 0x0412, "Intel HD Graphics 4200" },
	{ 0x0A22, 0x0A2E, "Intel Iris Graphics 5100" },
	{ 0x0D12, 0x0412, "Intel HD Graphics 4600" },
	{ 0x0D16, 0x0412, "Intel HD Graphics 4600" },
	{ 0x1612, 0x0000, "Intel HD Graphics 5600" },
	{ 0x1616, 0x0000, "Intel HD Graphics 5500" },
	{ 0x161E, 0x0000, "Intel HD Graphics 5300" },
	{ 0x1622, 0x0000, "Intel Iris Pro Graphics 6200" },
	{ 0x1626, 0x0000, "Intel HD Graphics 6000" },
	{ 0x162B, 0x0000, "Intel Iris Graphics 6100" },
	{ 0x162A, 0x0000, "Intel Iris Pro Graphics P6300" },
	{ 0x162D, 0x0000, "Intel Iris Pro Graphics P6300" },
	// Reserved/unused/generic Broadwell },
	// { 0x0BD1, 0x0000, nullptr },
	// { 0x0BD2, 0x0000, nullptr },
	// { 0x0BD3, 0x0000, nullptr },
	// { 0x1602, 0x0000, nullptr },
	// { 0x1606, 0x0000, nullptr },
	// { 0x160B, 0x0000, nullptr },
	// { 0x160A, 0x0000, nullptr },
	// { 0x160D, 0x0000, nullptr },
	// { 0x160E, 0x0000, nullptr },
	// { 0x161B, 0x0000, nullptr },
	// { 0x161A, 0x0000, nullptr },
	// { 0x161D, 0x0000, nullptr },
	// { 0x162E, 0x0000, nullptr },
	// { 0x1632, 0x0000, nullptr },
	// { 0x1636, 0x0000, nullptr },
	// { 0x163B, 0x0000, nullptr },
	// { 0x163A, 0x0000, nullptr },
	// { 0x163D, 0x0000, nullptr },
	// { 0x163E, 0x0000, nullptr },
	{ 0x1902, 0x0000, "Intel HD Graphics 510" },
	{ 0x1906, 0x0000, "Intel HD Graphics 510" },
	{ 0x190B, 0x0000, "Intel HD Graphics 510" },
	{ 0x191E, 0x0000, "Intel HD Graphics 515" },
	{ 0x1916, 0x0000, "Intel HD Graphics 520" },
	{ 0x1921, 0x0000, "Intel HD Graphics 520" },
	{ 0x1912, 0x0000, "Intel HD Graphics 530" },
	{ 0x191B, 0x0000, "Intel HD Graphics 530" },
	{ 0x191D, 0x191B, "Intel HD Graphics P530" },
	{ 0x1923, 0x191B, "Intel HD Graphics 535" },
	{ 0x1926, 0x0000, "Intel Iris Graphics 540" },
	{ 0x1927, 0x0000, "Intel Iris Graphics 550" },
	{ 0x192B, 0x0000, "Intel Iris Graphics 555" },
	{ 0x192D, 0x1927, "Intel Iris Graphics P555" },
	{ 0x1932, 0x0000, "Intel Iris Pro Graphics 580" },
	{ 0x193A, 0x193B, "Intel Iris Pro Graphics P580" },
	{ 0x193B, 0x0000, "Intel Iris Pro Graphics 580" },
	{ 0x193D, 0x193B, "Intel Iris Pro Graphics P580" },
	// Reserved/unused/generic Skylake },
	// { 0x0901, 0x0000, nullptr },
	// { 0x0902, 0x0000, nullptr },
	// { 0x0903, 0x0000, nullptr },
	// { 0x0904, 0x0000, nullptr },
	// { 0x190E, 0x0000, nullptr },
	// { 0x1913, 0x0000, nullptr },
	// { 0x1915, 0x0000, nullptr },
	// { 0x1917, 0x0000, nullptr },
    	{ 0x5902, 0x0000, "Intel HD Graphics 610" },
	{ 0x591E, 0x0000, "Intel HD Graphics 615" },
	{ 0x5916, 0x0000, "Intel HD Graphics 620" },
    	{ 0x5917, 0x0000, "Intel UHD Graphics 620" },
	{ 0x5912, 0x0000, "Intel HD Graphics 630" },
	{ 0x591B, 0x0000, "Intel HD Graphics 630" },
    	{ 0x591D, 0x591B, "Intel HD Graphics P630" },
	{ 0x5926, 0x0000, "Intel Iris Plus Graphics 640" },
	{ 0x5927, 0x0000, "Intel Iris Plus Graphics 650" },
	// Currently unsupported and needs to be faked to 0x3E92 (mobile i3)
	{ 0x3E91, 0x0000, "Intel UHD Graphics 630" },
	{ 0x3E92, 0x0000, "Intel UHD Graphics 630" },
	// Reserved/unused/generic Kaby Lake / Coffee Lake },
	// { 0x3E9B, 0x0000, nullptr },
	// { 0x3EA5, 0x0000, nullptr },
};

const char *getModelName(uint32_t device, uint32_t &fakeId) {
	fakeId = 0;
	for (size_t i = 0; i < arrsize(deviceModels); i++) {
		if (deviceModels[i].device == device) {
			fakeId = deviceModels[i].fake;
			return deviceModels[i].name;
		}
	}

	return nullptr;
}
