//
//  kern_guc.hpp
//  IGFX
//
//  Copyright © 2018 lvs1974. All rights reserved.
//

#ifndef kern_guc_hpp
#define kern_guc_hpp

#include <libkern/libkern.h>

static constexpr size_t GuCFirmwareSignatureSize = 256;
static constexpr size_t HuCFirmwareSignatureSize = 256;

extern const uint8_t *GuCFirmwareSKL;
extern const uint8_t *GuCFirmwareSKLSignature;
extern const size_t GuCFirmwareSKLSize;

extern const uint8_t *GuCFirmwareKBL;
extern const uint8_t *GuCFirmwareKBLSignature;
extern const size_t GuCFirmwareKBLSize;

extern const uint8_t *HuCFirmwareSKL;
extern const uint8_t *HuCFirmwareSKLSignature;
extern const size_t HuCFirmwareSKLSize;

extern const uint8_t *HuCFirmwareKBL;
extern const uint8_t *HuCFirmwareKBLSignature;
extern const size_t HuCFirmwareKBLSize;

#endif /* kern_guc_hpp */
