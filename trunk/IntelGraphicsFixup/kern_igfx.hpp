//
//  kern_igfx.hpp
//  IGFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#ifndef kern_igfx_hpp
#define kern_igfx_hpp

#include <Headers/kern_patcher.hpp>

class IGFX {
public:
	bool init();
	void deinit();
	
private:
	/**
	 *  Patch kext if needed and prepare other patches
	 *
	 *  @param patcher KernelPatcher instance
	 *  @param index   kinfo handle
	 *  @param address kinfo load address
	 *  @param size    kinfo memory size
	 */
	void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	
	/**
	 *  PAVP session command type
	 */
	using PAVPSessionCommandID_t = int32_t;
	
	/**
	 *  PAVP session callback type
	 */
	using t_pavp_session_callback = uint32_t (*)(void *, PAVPSessionCommandID_t, uint32_t, uint32_t *, bool);
		
		
	/**
	 *  frameBufferInit type
	 */
	using t_frame_buffer_init = void (*)(void *);
		
	/**
	 *  computeLaneCount type
	 */
	using t_compute_lane_count = bool (*)(void *, void *, unsigned int, int, int *);
		
	
	/**
	 *  Hooked methods / callbacks
	 */
	static uint32_t pavpSessionCallback(void *intelAccelerator, PAVPSessionCommandID_t passed_session_cmd, uint32_t a3, uint32_t *a4, bool passed_flag);
	static void frameBufferInit(void *that);
	static bool computeLaneCount(void *that, void *unk1, unsigned int bpp, int unk3, int *lane_count);

	/**
	 *  Trampolines for original method invocations
	 */
	t_pavp_session_callback orgPavpSessionCallback {nullptr};
	t_frame_buffer_init orgFrameBufferInit {nullptr};
	t_compute_lane_count orgComputeLaneCount {nullptr};
		
	/**
	 *  external global variables
	 */
	uint8_t *gIOFBVerboseBootPtr {nullptr};
	
	/**
	 *  Current progress mask
	 */
	struct ProcessingState {
		enum {
			NothingReady = 0,
			CallbackPavpSessionRouted = 1,
			CallbackPavpSessionHD4000Routed = 2,
			CallbackFrameBufferInitRouted = 4,
			CallbackComputeLaneCountRouted = 8,
			EverythingDone = CallbackPavpSessionRouted | CallbackPavpSessionHD4000Routed | CallbackFrameBufferInitRouted | CallbackComputeLaneCountRouted,
		};
	};
	int progressState {ProcessingState::NothingReady};
};

#endif /* kern_azul_pavp_disabler_hpp */
