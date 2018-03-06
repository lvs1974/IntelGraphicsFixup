//
//  kern_audio.hpp
//  IGFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#ifndef kern_audio_hpp
#define kern_audio_hpp

#include <Headers/kern_util.hpp>

#include <Library/LegacyIOService.h>
#include <sys/types.h>

class EXPORT IntelGraphicsAudio : public IOService {
	OSDeclareDefaultStructors(IntelGraphicsAudio)
public:
	static uint32_t getAnalogLayout();
	IOService *probe(IOService *provider, SInt32 *score) override;
};

#endif /* kern_audio_hpp */
