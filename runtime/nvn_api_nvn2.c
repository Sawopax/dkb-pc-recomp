// NVN 2 procs Donkey Kong Bananza asks for beyond TotK's NVN (runtime/nvn_procs_nvn2.inc). Only the calls whose effect
// the host can honour without knowing a new NVN 2 structure layout are here; the rest stop with their name
// (nvn_procs.c) until a call shows what they need.
#include <stdint.h>
#include <string.h>

#include "gfx.h"
#include "host.h"
#include "nvn_api.h"
#include "nvn_mode_trace.h"
#include "nvn_obj.h"

// ---- Queues. The host has one queue model: a queue's primary queue and its ray-tracing memory change nothing.
// void nvnQueueBuilderSetPrimaryQueue(NVNqueueBuilder *b, NVNqueue *primary)
NVN_API(nvnQueueBuilderSetPrimaryQueue) { NVN_LOG("QueueBuilderSetPrimaryQueue %p", arg_p(c, 1)); }
// void nvnQueueBuilderSetRayTracingMemorySize(NVNqueueBuilder *b, size_t n)
NVN_API(nvnQueueBuilderSetRayTracingMemorySize) { (void)c; }
// size_t nvnQueueBuilderGetRayTracingMemorySize(const NVNqueueBuilder *b)
NVN_API(nvnQueueBuilderGetRayTracingMemorySize) { ret_x(c, 0); }
// size_t nvnQueueGetTotalRayTracingMemoryUsed(const NVNqueue *q)
NVN_API(nvnQueueGetTotalRayTracingMemoryUsed) { ret_x(c, 0); }
// const NVNqueue *nvnQueueBuilderGetPrimaryQueue(const NVNqueueBuilder *b): the builder keeps none (update 458752)
NVN_API(nvnQueueBuilderGetPrimaryQueue) { ret_x(c, 0); }

// ---- Device
// int nvnDeviceGetFlags(const NVNdevice *d): the flags the device was initialized with
NVN_API(nvnDeviceGetFlags) { ret_x(c, ((const dev_t_ *)arg_p(c, 0))->flags); }
// NVNboolean nvnDeviceRegisterFastClear{Color,Colori,Colorui}Index(NVNdevice *, const T *color, NVNformat, int index),
// ...Stencil(NVNdevice *, int value): like the NVN 1 forms (nvn_api_core.c), "not registered" keeps the normal clear
NVN_API(nvnDeviceRegisterFastClearColorIndex) { ret_x(c, 0); }
NVN_API(nvnDeviceRegisterFastClearColoriIndex) { ret_x(c, 0); }
NVN_API(nvnDeviceRegisterFastClearColoruiIndex) { ret_x(c, 0); }
NVN_API(nvnDeviceRegisterFastClearStencil) { ret_x(c, 0); }
// ...DepthIndex and ...StencilIndex (update 458752): not registered either
NVN_API(nvnDeviceRegisterFastClearDepthIndex) { ret_x(c, 0); }
NVN_API(nvnDeviceRegisterFastClearStencilIndex) { ret_x(c, 0); }

// ---- Device message buffer (driver debug messages): the host driver writes none
NVN_API(nvnDeviceMessageBufferSetDefaults) { (void)c; }
NVN_API(nvnDeviceMessageBufferSetDevice) { (void)c; }
NVN_API(nvnDeviceMessageBufferSetStorage) { (void)c; }
NVN_API(nvnDeviceMessageBufferBeginMessages) { (void)c; }
NVN_API(nvnDeviceMessageBufferEndMessages) { (void)c; }
NVN_API(nvnDeviceMessageBufferReset) { (void)c; }
// const NVNdeviceMessage *nvnDeviceMessageBufferGetNextMessage(...): no message
NVN_API(nvnDeviceMessageBufferGetNextMessage) { ret_x(c, 0); }

// ---- Multisample state: coverage truncation is a hint the Vulkan back end has no switch for
NVN_API(nvnMultisampleStateSetCoverageTruncationEnable) { (void)c; }
NVN_API(nvnMultisampleStateGetCoverageTruncationEnable) { ret_x(c, 0); }
// void nvnMultisampleStateGetSampleLocations(const NVNmultisampleState *, int start, int count, float *xy): the host
// keeps no programmable locations (SetSampleLocations is ignored in nvn_api_obj.c), so every sample reads as the
// pixel centre. At most 16 samples.
NVN_API(nvnMultisampleStateGetSampleLocations) {
    int start = (int)arg_x(c, 1), count = (int)arg_x(c, 2);
    float *xy = arg_p(c, 3);
    if (!xy || start < 0 || count <= 0) return;
    if (count > 16) count = 16;
    for (int i = 0; i < 2 * count; i++) xy[i] = 0.5f;
}

// ---- Program builder ray-tracing modules: no ray tracing on the host, so no modules (update 458752)
NVN_API(nvnProgramBuilderGetRayTracingModuleCount) { ret_x(c, 0); }
NVN_API(nvnProgramBuilderGetRayTracingModuleData) { ret_x(c, 0); }

// ---- Dirty tile map parameters (update 458752): their only user, nvnCommandBufferComputeDirtyTileMapEx, is not
// recorded, so the setters keep nothing and the getters read zero
NVN_API(nvnDirtyTileMapParametersSetDefaults) { (void)c; }
NVN_API(nvnDirtyTileMapParametersSetConstantColorTable) { (void)c; }
NVN_API(nvnDirtyTileMapParametersSetDepthStencilMask) { (void)c; }
NVN_API(nvnDirtyTileMapParametersSetExpandFootprint) { (void)c; }
NVN_API(nvnDirtyTileMapParametersSetInputs) { (void)c; }
NVN_API(nvnDirtyTileMapParametersSetOutputs) { (void)c; }
NVN_API(nvnDirtyTileMapParametersSetTexture) { (void)c; }
NVN_API(nvnDirtyTileMapParametersGetConstantColorTable) { ret_x(c, 0); }
NVN_API(nvnDirtyTileMapParametersGetDepthStencilMask) { ret_x(c, 0); }
NVN_API(nvnDirtyTileMapParametersGetExpandFootprint) { ret_x(c, 0); }
NVN_API(nvnDirtyTileMapParametersGetInputs) { ret_x(c, 0); }
NVN_API(nvnDirtyTileMapParametersGetOutputs) { ret_x(c, 0); }
NVN_API(nvnDirtyTileMapParametersGetTexture) { ret_x(c, 0); }

// ---- DLSS network queries and resize (update 458752): the host never initializes a DLSS network, so these are
// reached only if the game asks about one it did not make; they answer nothing and fail
NVN_API(nvnDlssNetworkBuilderGetTextureIDs) { ret_x(c, 0); }
NVN_API(nvnDlssNetworkBuilderGetTexturePool) { ret_x(c, 0); }
NVN_API(nvnDlssNetworkResize) { ret_x(c, 0); }

// ---- Window colour space and HDR metadata: the host presents SDR sRGB
// void nvnWindowBuilderSetColorSpace(NVNwindowBuilder *, NVNcolorSpace), NVNcolorSpace ...GetColorSpace(...): 0, sRGB
NVN_API(nvnWindowBuilderSetColorSpace) { NVN_LOG("WindowBuilderSetColorSpace %d", (int)arg_x(c, 1)); }
NVN_API(nvnWindowBuilderGetColorSpace) { ret_x(c, 0); }
// void ...Set{Hdr}Display/ContentMetadata(obj, const metadata *): kept by no one
NVN_API(nvnWindowBuilderSetHdrDisplayMetadata) { (void)c; }
NVN_API(nvnWindowBuilderSetHdrContentMetadata) { (void)c; }
NVN_API(nvnWindowSetHdrDisplayMetadata) { (void)c; }
NVN_API(nvnWindowSetHdrContentMetadata) { (void)c; }

// void nvnCommandBufferSetComputeOverlapMode(NVNcommandBuffer *cmd, uint32_t mode)
// NVN 2 changes compute/graphics scheduling, not shader or resource state (docs/nvn2-command-modes.md).
// Our dispatch and graphics-pass boundaries already insert full barriers on one queue, so both scheduling modes
// retain that conservative ordering. Allowing overlap does not require the host to execute work concurrently.
NVN_API(nvnCommandBufferSetComputeOverlapMode) { (void)c; }

// ---- Command buffer calls new in NVN 2. Until the Vulkan back end learns them (mesh shaders, ray tracing, DLSS,
// variable-rate shading, conditional enables, device command buffers), they are accepted and
// recorded nowhere, and the first call of each is logged, so a run lists what the back end still has to do. None of
// them returns a value.
#define NVN2_NOT_RECORDED(n)                                                                                         \
    NVN_API(n) {                                                                                                     \
        static volatile int32_t logged;                                                                              \
        (void)c;                                                                                                     \
        if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) rt_log("nvn2: " #n " accepted, not recorded yet");   \
    }
#define NVN2_MODE_NOT_RECORDED(n, kind)                                                                               \
    NVN_API(n) {                                                                                                    \
        static volatile int32_t logged;                                                                             \
        nvn_mode_trace(kind, arg_x(c, 0), 0, (uint32_t)arg_x(c, 1), 0);                                                \
        if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) rt_log("nvn2: " #n " accepted, not recorded yet");  \
    }
NVN2_MODE_NOT_RECORDED(nvnCommandBufferSetRecordingMode, NVN_MODE_RECORDING)
NVN2_NOT_RECORDED(nvnCommandBufferBeginDeviceCommands)
// New in update 458752 (NVN API 231.30)
NVN2_NOT_RECORDED(nvnCommandBufferComputeDirtyTileMapEx)
NVN2_NOT_RECORDED(nvnCommandBufferGetAccelerationStructureVisualizationData)
NVN2_NOT_RECORDED(nvnCommandBufferSetComputeWorkloadPriority)
NVN2_NOT_RECORDED(nvnCommandBufferSetGraphicsWorkloadPriority)
NVN2_NOT_RECORDED(nvnCommandBufferSetResourceOccupancy)
NVN2_NOT_RECORDED(nvnCommandBufferSetFloatBlendClamp)
NVN2_NOT_RECORDED(nvnCommandBufferEndDeviceCommands)
NVN2_NOT_RECORDED(nvnCommandBufferSubmitDeviceCommands)
NVN2_NOT_RECORDED(nvnCommandBufferBindAccelerationStructure)
NVN2_NOT_RECORDED(nvnCommandBufferBindAccelerationStructures)
NVN2_NOT_RECORDED(nvnCommandBufferBindAccelerationStructuresDeferred)
NVN2_NOT_RECORDED(nvnCommandBufferBuildAccelerationStructures)
NVN2_NOT_RECORDED(nvnCommandBufferCompactAccelerationStructure)
NVN2_NOT_RECORDED(nvnCommandBufferCopyAccelerationStructure)
NVN2_NOT_RECORDED(nvnCommandBufferRelocateAccelerationStructure)
NVN2_NOT_RECORDED(nvnCommandBufferDispatchRays)
NVN2_NOT_RECORDED(nvnCommandBufferDispatchRaysIndirect)
NVN2_NOT_RECORDED(nvnCommandBufferBindDeviceMessageBuffer)
NVN2_NOT_RECORDED(nvnCommandBufferBindDeviceMessageBufferDeferred)
NVN2_NOT_RECORDED(nvnCommandBufferResetDeviceMessageBuffer)
NVN2_NOT_RECORDED(nvnCommandBufferResetDeviceMessageBufferDeferred)
NVN2_NOT_RECORDED(nvnCommandBufferBindShadingRateImage)
NVN2_NOT_RECORDED(nvnCommandBufferSetShadingRatePaletteMode)
NVN2_NOT_RECORDED(nvnCommandBufferShadingRateImagePalette)
NVN2_NOT_RECORDED(nvnCommandBufferComputeDirtyTileMap)
NVN2_NOT_RECORDED(nvnCommandBufferDrawMeshTasks)
NVN2_NOT_RECORDED(nvnCommandBufferDrawMeshTasks3D)
NVN2_NOT_RECORDED(nvnCommandBufferDrawMeshTasks3DIndirect)
NVN2_NOT_RECORDED(nvnCommandBufferDrawMeshTasksIndirect)
NVN2_NOT_RECORDED(nvnCommandBufferMultiDrawMeshTasks3DIndirect)
NVN2_NOT_RECORDED(nvnCommandBufferMultiDrawMeshTasks3DIndirectCount)
NVN2_NOT_RECORDED(nvnCommandBufferMultiDrawMeshTasksIndirect)
NVN2_NOT_RECORDED(nvnCommandBufferMultiDrawMeshTasksIndirectCount)
NVN2_NOT_RECORDED(nvnCommandBufferEnableExclusiveScissor)
NVN2_NOT_RECORDED(nvnCommandBufferSetExclusiveScissor)
NVN2_NOT_RECORDED(nvnCommandBufferSetExclusiveScissors)
NVN2_NOT_RECORDED(nvnCommandBufferEvaluateDlss)
NVN2_NOT_RECORDED(nvnCommandBufferInsertDebugMarkerCompute)
NVN2_NOT_RECORDED(nvnCommandBufferInsertDebugMarkerGraphics)
NVN2_NOT_RECORDED(nvnCommandBufferPushDebugGroupCompute)
NVN2_NOT_RECORDED(nvnCommandBufferPushDebugGroupGraphics)
NVN2_NOT_RECORDED(nvnCommandBufferPopDebugGroupCompute)
NVN2_NOT_RECORDED(nvnCommandBufferPopDebugGroupGraphics)
// nvnCommandBufferPush{Graphics,Compute}Constants and their Deferred forms are recorded in nvn_api_cmd.c (OP_PUSH_CONST).
NVN2_NOT_RECORDED(nvnCommandBufferSetComputeEnable)
NVN2_NOT_RECORDED(nvnCommandBufferSetComputeEnableConditional)
NVN2_NOT_RECORDED(nvnCommandBufferSetCopyEnable)
NVN2_NOT_RECORDED(nvnCommandBufferSetCopyEnableConditional)
NVN2_NOT_RECORDED(nvnCommandBufferSetConservativeRasterMode)
NVN2_MODE_NOT_RECORDED(nvnCommandBufferSetGeometryProcessingMode, NVN_MODE_GEOMETRY)
NVN2_NOT_RECORDED(nvnCommandBufferSetRepresentativeFragmentTest)
