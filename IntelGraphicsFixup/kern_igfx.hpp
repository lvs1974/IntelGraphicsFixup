//
//  kern_igfx.hpp
//  IGFX
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#ifndef kern_igfx_hpp
#define kern_igfx_hpp

#include <Headers/kern_patcher.hpp>
#include <Headers/kern_cpu.hpp>

class IGFX {
public:
	bool init();
	void deinit();

private:
	/**
	 *  Obtain necessary symbols from the kernel
	 *
	 *  @param patcher KernelPatcher instance
	 */
	void processKernel(KernelPatcher &patcher);

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
	 *  AppleIntelXXXXGraphics::start callback type
	 */
	using t_intel_graphics_start = bool (*)(IOService *that, IOService *);

	/**
	 *  IGHardwareGuC::loadGuCBinary callback type
	 */
	using t_load_guc_binary = bool (*)(void *that);

	/**
	 *  IGScheduler4::loadFirmware callback type
	 */
	using t_load_firmware = bool (*)(void *that);

	/**
	 *  IGHardwareGuC::initSchedControl callback type
	 */
	using t_init_sched_control = bool (*)(void *that);

	/**
	 *  IGSharedMappedBuffer::withOptions callback type
	 */
	using t_ig_buffer_with_options = void *(*)(void *accelTask, unsigned long size, unsigned int type, unsigned int flags);

	/**
	 *  IGSharedMappedBuffer::getGPUVirtualAddress callback type
	 */
	using t_ig_get_gpu_vaddr = void *(*)(void *that);

	/**
	 *  Hooked methods / callbacks
	 */
	static uint32_t pavpSessionCallback(void *intelAccelerator, PAVPSessionCommandID_t passed_session_cmd, uint32_t a3, uint32_t *a4, bool passed_flag);
	static void frameBufferInit(void *that);
	static bool computeLaneCount(void *that, void *unk1, unsigned int bpp, int unk3, int *lane_count);
	static bool intelGraphicsStart(IOService *that, IOService *provider);
	static bool loadGuCBinary(void *that);
	static bool loadFirmware(IOService *that);
	static void systemWillSleep(IOService *that);
	static void systemDidWake(IOService *that);
	static bool initSchedControl(void *that);
	static void *igBufferWithOptions(void *accelTask, unsigned long size, unsigned int type, unsigned int flags);
	static void *igBufferGetGpuVirtualAddress(void *that);

	/**
	 *  Trampolines for original method invocations
	 */
	t_pavp_session_callback orgPavpSessionCallback {nullptr};
	t_frame_buffer_init orgFrameBufferInit {nullptr};
	t_compute_lane_count orgComputeLaneCount {nullptr};
	t_intel_graphics_start orgGraphicsStart {nullptr};
	t_load_guc_binary orgLoadGuCBinary {nullptr};
	t_load_firmware orgLoadFirmware {nullptr};
	t_init_sched_control orgInitSchedControl {nullptr};
	t_ig_buffer_with_options orgIgBufferWithOptions {nullptr};
	t_ig_get_gpu_vaddr orgIgGetGpuVirtualAddress {nullptr};

	/**
	 *  External global variables
	 */
	uint8_t *gIOFBVerboseBootPtr {nullptr};
	uint8_t *gKmGen9GuCBinary {nullptr};

	enum FramebufferFixMode {
		FBDEFAULT  = 0,
		FBRESET    = 1,
		FBCOPY     = 2
	};

	/**
	 *  Framebuffer distortion fix mode
	 */
	uint32_t resetFramebuffer {FBDEFAULT};

	/**
	 *  Framebuffer firmware loading mode, enable by default
	 */
	int32_t decideLoadFirmware {1};

	/**
	 *  CPU generation
	 */
	CPUInfo::CpuGeneration cpuGeneration {CPUInfo::CpuGeneration::Unknown};

	/**
	 *  Console info structure, taken from osfmk/console/video_console.h
	 *  Last updated from XNU 4570.1.46.
	 */
	struct vc_info {
		unsigned int   v_height;        /* pixels */
		unsigned int   v_width;         /* pixels */
		unsigned int   v_depth;
		unsigned int   v_rowbytes;
		unsigned long  v_baseaddr;
		unsigned int   v_type;
		char           v_name[32];
		uint64_t       v_physaddr;
		unsigned int   v_rows;          /* characters */
		unsigned int   v_columns;       /* characters */
		unsigned int   v_rowscanbytes;  /* Actualy number of bytes used for display per row*/
		unsigned int   v_scale;
		unsigned int   v_rotate;
		unsigned int   v_reserved[3];
	};

	/**
	 *  vinfo presence status
	 */
	bool gotInfo {false};

	/**
	 *  connector-less frame
	 */
	bool connectorLessFrame {false};

	/**
	 *  Loaded vinfo
	 */
	vc_info vinfo {};

	/**
	 *  Console buffer backcopy
	 */
	uint8_t *consoleBuffer {nullptr};

	/**
	 *  We are currently trying to load the firmware
	 */
	bool performingFirmwareLoad {false};

	/**
	 *  Dummy firmware buffer to store unused old firmware in
	 */
	uint8_t *dummyFirmwareBuffer {nullptr};

	/**
	 *  Actual firmware buffer we store our new firmware in
	 */
	uint8_t *realFirmwareBuffer {nullptr};

	/**
	 *  Pointer to the size assignment
	 */
	uint32_t *firmwareSizePointer {nullptr};

	/**
	 *  Pointer to the signature
	 */
	uint8_t *signaturePointer {nullptr};

	/**
	 *  Current progress mask
	 */
	struct ProcessingState {
		enum {
			NothingReady = 0,
			CallbackPavpSessionRouted = 1,
			CallbackPavpSessionHD3000Routed = 2,
			CallbackPavpSessionHD4000Routed = 4,
			CallbackFrameBufferInitRouted = 8,
			CallbackComputeLaneCountRouted = 16,
			CallbackDriverStartRouted = 32,
			CallbackGuCFirmwareUpdateRouted = 64,
			EverythingDone = CallbackPavpSessionRouted |
				CallbackPavpSessionHD3000Routed |
				CallbackPavpSessionHD4000Routed |
				CallbackFrameBufferInitRouted |
				CallbackComputeLaneCountRouted |
				CallbackDriverStartRouted |
				CallbackGuCFirmwareUpdateRouted,
		};
	};
	int progressState {ProcessingState::NothingReady};
};

#endif /* kern_azul_pavp_disabler_hpp */
