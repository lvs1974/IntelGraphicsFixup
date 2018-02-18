//
//  kern_model.cpp
//  IntelGraphicsFixup
//
//  Copyright © 2018 lvs1974. All rights reserved.
//

#include <stdint.h>

#include "kern_model.hpp"

const char *getModelName(uint32_t device) {
	switch (device) {
		case 0x0106:
		case 0x0601:
		case 0x0102:
			return "Intel HD Graphics 2000";
		case 0x0116:
		case 0x0126:
			return "Intel HD Graphics 3000";
		case 0x0152:
		case 0x0156:
			return "Intel HD Graphics 2500";
		case 0x0162:
		case 0x0166:
			return "Intel HD Graphics 4000";
		case 0x0d26:
		case 0x0d22:
			return "Intel Iris Pro Graphics 5200";
		case 0x0a26:
			return "Intel HD Graphics 5000";
		case 0x0a2e:
			return "Intel Iris Graphics 5100";
		case 0x0412:
			return "Intel HD Graphics 4600";
		case 0x1612:
			return "Intel HD Graphics 5600";
		case 0x1616:
			return "Intel HD Graphics 5500";
		case 0x161E:
			return "Intel HD Graphics 5300";
		case 0x1622:
			return "Intel Iris Pro Graphics 6200";
		case 0x1626:
			return "Intel HD Graphics 6000";
		case 0x162B:
			return "Intel Iris Graphics 6100";
		case 0x162A:
		case 0x162D:
			return "Intel Iris Pro Graphics P6300";
		// Reserved/unused/generic Broadwell
		case 0x0BD1:
		case 0x0BD2:
		case 0x0BD3:
		case 0x1602:
		case 0x1606:
		case 0x160B:
		case 0x160A:
		case 0x160D:
		case 0x160E:
		case 0x161B:
		case 0x161A:
		case 0x161D:
		case 0x162E:
		case 0x1632:
		case 0x1636:
		case 0x163B:
		case 0x163A:
		case 0x163D:
		case 0x163E:
			return nullptr;
		case 0x1902:
		case 0x1906:
		case 0x190B:
			return "Intel HD Graphics 510";
		case 0x191E:
			return "Intel HD Graphics 515";
		case 0x1916:
		case 0x1921:
			return "Intel HD Graphics 520";
		case 0x1912:
		case 0x191B:
			return "Intel HD Graphics 530";
		case 0x1926:
			return "Intel Iris Graphics 540";
		case 0x1927:
			return "Intel Iris Graphics 550";
		case 0x192B:
			return "Intel Iris Graphics 555";
		case 0x1932:
		case 0x193B:
			return "Intel Iris Pro Graphics 580";
		// Reserved/unused/generic Skylake
		case 0x0901:
		case 0x0902:
		case 0x0903:
		case 0x0904:
		case 0x190E:
		case 0x1913:
		case 0x1915:
		case 0x1917:
			return nullptr;
		case 0x591E:
			return "Intel HD Graphics 615";
		case 0x5916:
			return "Intel HD Graphics 620";
		case 0x5912:
		case 0x591B:
			return "Intel HD Graphics 630";
		case 0x5926:
			return "Intel Iris Plus Graphics 640";
		case 0x5927:
			return "Intel Iris Plus Graphics 650";
		case 0x3E92:
			return "Intel UHD Graphics 630";
		// Reserved/unused/generic Kaby Lake / Coffee Lake
		case 0x3E9B:
		case 0x3EA5:
			return nullptr;
		default:
			return nullptr;
	}
}
