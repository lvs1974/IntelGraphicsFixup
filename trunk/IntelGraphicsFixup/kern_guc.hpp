//
//  kern_guc.hpp
//  IGFX
//
//  Copyright © 2018 lvs1974. All rights reserved.
//

#ifndef kern_guc_h
#define kern_guc_h

#include <libkern/libkern.h>

static constexpr size_t GuCFirmwareSignatureSize = 256;

extern const uint8_t *GuCFirmwareSKL;
extern const uint8_t *GuCFirmwareSKLSignature;
extern const size_t GuCFirmwareSKLSize;

extern const uint8_t *GuCFirmwareKBL;
extern const uint8_t *GuCFirmwareKBLSignature;
extern const size_t GuCFirmwareKBLSize;

#endif /* kern_guc_h */
