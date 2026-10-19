/*
 * ffx_functions_q2rtx.c
 *
 * Implements the versioned FSR4-v07 provider entry points without linking
 * AMD's signed DLL.
 *
 * Instead of going through the provider/DLL layer, all calls route through
 * an explicit FfxInterface installed before context creation.
 *
 * The FSR4 upscaler context is modelled as a thin wrapper that stores the
 * FfxInterface pointer and the per-frame cbuffer data.  The 14-pass compute
 * dispatch is issued directly by calling fpExecuteGpuJobs via the backend.
 *
 * Jitter is computed with a Halton[2,3] sequence matching what the AMD SDK
 * generates (see ffxQueryDescUpscaleGetJitterOffset handling below).
 */

#include "ffx_vk_fsr4_v07_types.h"
#include "ffx_vk_fsr4_v07.h"
#include "ffx_vk_fsr4_v07_schedule.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* The interface is copied during context creation; it is not a renderer
 * global and does not need to remain installed for dispatch/destroy. */
static FfxInterface *g_fsr4_backend_interface;

void
ffxFsr4V07SetBackendInterface(FfxInterface *backend)
{
    g_fsr4_backend_interface = backend;
}

static uint16_t float_to_half(float value)
{
    union { float f; int32_t si; uint32_t ui; } v, s;
    const int32_t inf_n = 0x7f800000;
    const int32_t max_n = 0x477fe000;
    const int32_t min_n = 0x38800000;
    const int32_t sign_n = (int32_t)0x80000000u;
    const int32_t nan_n = 0x7f802000;
    const int32_t max_c = 0x23bff;
    const int32_t mul_n = 0x52000000;
    const int32_t sub_c = 0x003ff;
    const int32_t max_d = 0x1c000;
    const int32_t min_d = 0x1c000;
    uint32_t sign;

    v.f = value;
    sign = (uint32_t)v.si & (uint32_t)sign_n;
    v.si ^= (int32_t)sign;
    sign >>= 16u;
    s.si = mul_n;
    s.si = (int32_t)(s.f * v.f);
    v.si ^= (s.si ^ v.si) & -(min_n > v.si);
    v.si ^= (inf_n ^ v.si) & -((inf_n > v.si) & (v.si > max_n));
    v.si ^= (nan_n ^ v.si) & -((nan_n > v.si) & (v.si > inf_n));
    v.ui >>= 13u;
    v.si ^= ((v.si - max_d) ^ v.si) & -(v.si > max_c);
    v.si ^= ((v.si - min_d) ^ v.si) & -(v.si > sub_c);
    return (uint16_t)(v.ui | sign);
}

/* ── internal context ────────────────────────────────────────────────────── */

typedef struct Fsr4Context {
    FfxInterface        iface;          /* copy of backend interface        */
    FfxUInt32           effectId;       /* effectContextId from backend     */
    FfxApiDimensions2D  maxRenderSize;
    FfxApiDimensions2D  maxUpscaleSize;
    uint32_t            flags;
    bool                valid;

    /* ── Internal resources (created once, persist across frames) ────────── */
    FfxResourceInternal ri_scratch;       /* Large SSBO for NN activations   */
    FfxResourceInternal ri_initializer;   /* Immutable generated model data  */
    FfxResourceInternal ri_history;       /* Previous upscaled output (dpy)  */
    FfxResourceInternal ri_recurrent[2];  /* Recurrent feature maps (PP)     */
    FfxResourceInternal ri_reprojected;   /* Pre-pass → post-pass bridge     */
    FfxResourceInternal ri_rcas_temp;     /* Post-pass output when sharpening */
    FfxResourceInternal ri_exposure;      /* Current/previous exposure (2×1) */
    FfxResourceInternal ri_spd_atomic;    /* SPD global workgroup counter    */
    FfxResourceInternal ri_spd_mip5;      /* SPD intermediate luma mip       */
    int                 recurrent_idx;    /* 0 or 1: which is "current read" */
    float               previous_pre_exposure;
    bool                auto_exposure_last_enabled;

    bool                resources_ok;     /* true if internal resources exist */
} Fsr4Context;

/* ── Halton sequence helper ──────────────────────────────────────────────── */

static float halton(int base, int index)
{
    float f = 1.0f, r = 0.0f;
    int   i = index;
    while (i > 0) {
        f /= (float)base;
        r += f * (float)(i % base);
        i /= base;
    }
    return r;
}

static int jitter_phase_count(uint32_t display_w, uint32_t render_w)
{
    /* FSR4 formula: ceil(8 * (display_w / render_w)^2), clamped to sensible range */
    if (render_w == 0) return 8;
    float ratio = (float)display_w / (float)render_w;
    int   count = (int)ceilf(8.0f * ratio * ratio);
    if (count < 1)   count = 1;
    if (count > 256) count = 256;
    return count;
}

/* ── Internal resource creation helper ───────────────────────────────────── */

static FfxErrorCode create_internal_tex(Fsr4Context *c, uint32_t w, uint32_t h,
    FfxSurfaceFormat fmt, FfxResourceInternal *out)
{
    FfxCreateResourceDescription desc;
    memset(&desc, 0, sizeof(desc));
    desc.type    = FFX_RESOURCE_TYPE_TEXTURE2D;
    desc.format  = fmt;
    desc.width   = w;
    desc.height  = h;
    return c->iface.fpCreateResource(&c->iface, &desc, c->effectId, out);
}

static FfxErrorCode create_internal_buf(Fsr4Context *c, uint32_t size_bytes,
    const void *initial_data,
    FfxResourceInternal *out)
{
    FfxCreateResourceDescription desc;
    memset(&desc, 0, sizeof(desc));
    desc.type         = FFX_RESOURCE_TYPE_BUFFER;
    desc.format       = FFX_SURFACE_FORMAT_UNKNOWN;
    desc.width        = size_bytes;  /* buffer "width" = size in bytes */
    desc.height       = 0;
    desc.initDataSize = size_bytes;
    desc.initData     = initial_data;
    return c->iface.fpCreateResource(&c->iface, &desc, c->effectId, out);
}

static FfxErrorCode create_internal_resources(Fsr4Context *c)
{
    FfxErrorCode err;
    uint32_t dw = c->maxUpscaleSize.width;
    uint32_t dh = c->maxUpscaleSize.height;
    size_t initializer_size = 0;
    const void *initializer =
        ffxFsr4VkGetModelInitializer(&c->iface, &initializer_size);

    /* These are generated-code ABI sizes, not resolution estimates. */
    size_t scratch_size = ffxFsr4GetDot4ScratchSize(dw, dh);
    if (!scratch_size || scratch_size > UINT32_MAX ||
        !initializer || !initializer_size ||
        initializer_size > UINT32_MAX)
        return FFX_ERROR_INVALID_ARGUMENT;

    err = create_internal_buf(c, (uint32_t)scratch_size, NULL, &c->ri_scratch);
    if (err != FFX_OK) return err;
    err = create_internal_buf(c, (uint32_t)initializer_size, initializer,
                              &c->ri_initializer);
    if (err != FFX_OK) return err;

    /* History color — previous frame's upscaled output (display resolution) */
    err = create_internal_tex(c, dw, dh, FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                              &c->ri_history);
    if (err != FFX_OK) return err;

    /* Recurrent feature maps — two copies for ping-pong (display resolution).
       The provider declares these as RGBA8_UNORM and the generated post-pass
       SPIR-V is explicitly reflected as Rgba8.  RGBA32F changes the trained
       recurrent-state quantization and is not ABI-compatible. */
    for (int i = 0; i < 2; i++) {
        err = create_internal_tex(c, dw, dh, FFX_SURFACE_FORMAT_R8G8B8A8_UNORM,
                                  &c->ri_recurrent[i]);
        if (err != FFX_OK) return err;
    }

    /* Reprojected color — pre-pass output (display resolution) */
    err = create_internal_tex(c, dw, dh, FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                              &c->ri_reprojected);
    if (err != FFX_OK) return err;

    /* RCAS reads the post pass' output and writes the application's final
     * output image. Keep the intermediate internal so the temporal/output
     * resource contract remains identical with sharpening on or off. */
    err = create_internal_tex(c, dw, dh, FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                              &c->ri_rcas_temp);
    if (err != FFX_OK) return err;

    /* The SPD shader stores current and previous exposure in x=0/1. The
     * trained passes read x=0, which also gives explicit exposure a stable
     * identity fallback when SPD is disabled. */
    err = create_internal_tex(c, 2, 1, FFX_SURFACE_FORMAT_R32_FLOAT,
                              &c->ri_exposure);
    if (err != FFX_OK) return err;
    err = create_internal_tex(c, 1, 1, FFX_SURFACE_FORMAT_R32_UINT,
                              &c->ri_spd_atomic);
    if (err != FFX_OK) return err;
    err = create_internal_tex(c, (c->maxRenderSize.width + 15u) / 16u,
                              (c->maxRenderSize.height + 15u) / 16u,
                              FFX_SURFACE_FORMAT_R32_FLOAT, &c->ri_spd_mip5);
    if (err != FFX_OK) return err;

    /* Persistent temporal images and the explicit-exposure fallback must
     * start from deterministic contents.  This happens once per context;
     * clearing them every frame would destroy temporal accumulation. */
    {
        const FfxResourceInternal clear_targets[] = {
            c->ri_history,
            c->ri_recurrent[0],
            c->ri_recurrent[1],
            c->ri_reprojected,
            c->ri_rcas_temp,
            c->ri_spd_atomic
        };
        for (size_t i = 0; i < sizeof(clear_targets) / sizeof(clear_targets[0]); ++i) {
            FfxGpuJobDescription clear_job;
            memset(&clear_job, 0, sizeof(clear_job));
            clear_job.jobType = FFX_GPU_JOB_CLEAR_FLOAT;
            clear_job.clearJobDescriptor.target = clear_targets[i];
            err = c->iface.fpScheduleGpuJob(&c->iface, &clear_job);
            if (err != FFX_OK) return err;
        }
        {
            FfxGpuJobDescription clear_job;
            memset(&clear_job, 0, sizeof(clear_job));
            clear_job.jobType = FFX_GPU_JOB_CLEAR_FLOAT;
            clear_job.clearJobDescriptor.target = c->ri_exposure;
            clear_job.clearJobDescriptor.color[0] = 1.0f;
            err = c->iface.fpScheduleGpuJob(&c->iface, &clear_job);
            if (err != FFX_OK) return err;
        }
    }

    c->recurrent_idx = 0;
    c->previous_pre_exposure = 1.0f;
    c->auto_exposure_last_enabled = false;
    c->resources_ok  = true;
    return FFX_OK;
}

/* ── ffxCreateContext ─────────────────────────────────────────────────────── */

ffxReturnCode_t ffxFsr4V07CreateContext(ffxContext *ctx,
    ffxCreateContextDescHeader *desc,
    const ffxAllocationCallbacks *mem)
{
    (void)mem;

    if (!ctx || !desc) return FFX_API_RETURN_ERROR_PARAMETER;
    if (!g_fsr4_backend_interface) return FFX_API_RETURN_ERROR;

    /* Find the upscale descriptor in the linked list */
    ffxCreateContextDescUpscale *upscaleDesc = NULL;
    for (ffxCreateContextDescHeader *h = desc; h; h = h->pNext) {
        if (h->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE) {
            upscaleDesc = (ffxCreateContextDescUpscale *)h;
            break;
        }
    }
    if (!upscaleDesc) return FFX_API_RETURN_ERROR_PARAMETER;
    if (!upscaleDesc->maxRenderSize.width ||
        !upscaleDesc->maxRenderSize.height ||
        !upscaleDesc->maxUpscaleSize.width ||
        !upscaleDesc->maxUpscaleSize.height ||
        upscaleDesc->maxUpscaleSize.width > FFX_FSR4_DOT4_MAX_OUTPUT_WIDTH ||
        upscaleDesc->maxUpscaleSize.height > FFX_FSR4_DOT4_MAX_OUTPUT_HEIGHT)
        return FFX_API_RETURN_ERROR_PARAMETER;

    Fsr4Context *c = (Fsr4Context *)calloc(1, sizeof(Fsr4Context));
    if (!c) return FFX_API_RETURN_ERROR;

    /* Copy the interface so the caller may clear its creation binding. */
    c->iface          = *g_fsr4_backend_interface;
    c->maxRenderSize  = upscaleDesc->maxRenderSize;
    c->maxUpscaleSize = upscaleDesc->maxUpscaleSize;
    c->flags          = upscaleDesc->flags;
    c->valid          = true;

    /* Initialise the Vulkan backend context (creates pipelines) */
    FfxEffectBindlessConfig bindlessCfg = {0, 0, 0, 1};
    FfxErrorCode err = c->iface.fpCreateBackendContext(
        &c->iface, FFX_EFFECT_FSR4UPSCALER, &bindlessCfg, &c->effectId);
    if (err != FFX_OK) {
        free(c);
        return FFX_API_RETURN_ERROR;
    }

    /* Create internal GPU resources (scratch, history, recurrent, etc.) */
    err = create_internal_resources(c);
    if (err != FFX_OK) {
        c->iface.fpDestroyBackendContext(&c->iface, c->effectId);
        free(c);
        return FFX_API_RETURN_ERROR;
    }

    *ctx = (ffxContext)c;
    return FFX_API_RETURN_OK;
}

/* ── ffxDestroyContext ────────────────────────────────────────────────────── */

ffxReturnCode_t ffxFsr4V07DestroyContext(ffxContext *ctx,
    const ffxAllocationCallbacks *mem)
{
    (void)mem;
    if (!ctx || !*ctx) return FFX_API_RETURN_ERROR_PARAMETER;

    Fsr4Context *c = (Fsr4Context *)(*ctx);
    if (c->valid)
        c->iface.fpDestroyBackendContext(&c->iface, c->effectId);

    free(c);
    *ctx = NULL;
    return FFX_API_RETURN_OK;
}

/* ── ffxQuery ─────────────────────────────────────────────────────────────── */

ffxReturnCode_t ffxFsr4V07Query(ffxContext *ctx, ffxQueryDescHeader *desc)
{
    if (!desc) return FFX_API_RETURN_ERROR_PARAMETER;

    Fsr4Context *c = (ctx && *ctx) ? (Fsr4Context *)(*ctx) : NULL;

    for (ffxQueryDescHeader *h = desc; h; h = h->pNext) {

        if (h->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_JITTER_PHASE_COUNT) {
            ffxQueryDescUpscaleGetJitterPhaseCount *q =
                (ffxQueryDescUpscaleGetJitterPhaseCount *)h;
            if (q->pOutPhaseCount)
                *q->pOutPhaseCount = jitter_phase_count(
                    q->displayWidth, q->renderWidth);
            continue;
        }

        if (h->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_JITTER_OFFSET) {
            ffxQueryDescUpscaleGetJitterOffset *q =
                (ffxQueryDescUpscaleGetJitterOffset *)h;
            if (q->pOutX && q->pOutY) {
                /* Halton[2,3], 1-indexed, shifted to [-0.5, 0.5] */
                int idx = (q->index % q->phaseCount) + 1;
                *q->pOutX = halton(2, idx) - 0.5f;
                *q->pOutY = halton(3, idx) - 0.5f;
            }
            continue;
        }
    }

    (void)c;
    return FFX_API_RETURN_OK;
}

/* ── ffxDispatch ─────────────────────────────────────────────────────────── */

/*
 * Dispatch a single upscale frame.
 *
 * The FSR4 INT8 model runs as 14 sequential compute passes (pre + 12 model
 * passes + post), optionally followed by RCAS sharpening.  The backend
 * (ffx_fsr4_vk.c) already has the VkPipeline objects built for each pass;
 * we just need to schedule the right dispatch jobs.
 *
 * Resource → register → Vulkan binding mapping:
 *
 *   FULL passes (pre/post) compiled with -fvk-t-shift 0 -fvk-u-shift 21 -fvk-b-shift 43:
 *     srvTextures[i]  →  binding i            (COMBINED_IMAGE_SAMPLER)
 *     uavTextures[i]  →  binding 21+i         (STORAGE_IMAGE)
 *     cbs[0]          →  binding 43            (UNIFORM_BUFFER)
 *     uavBuffers (scratch) → binding 32        (STORAGE_BUFFER, via bindingIndex)
 *
 *   writeFullDs iterates srvTextures[0..srvTextureCount-1] and writes each
 *   at Vulkan binding = SLOT_SRV_TEX_BASE + loop_index.  So srvTextures[]
 *   MUST be indexed by t-register number: srvTextures[3] → t3 → binding 3.
 *   Unused slots must have internalIndex = UINT32_MAX so writeFullDs skips them.
 *
 *   Same for uavTextures: uavTextures[i] → binding 21+i.  So uavTextures[3]
 *   → u3 → binding 24.  Pad unused slots with UINT32_MAX.
 *
 *   MODEL passes compiled with -fvk-t-shift 0 -fvk-u-shift 2:
 *     srvBuffers[0] → binding 0 (t0 = input)
 *     srvBuffers[1] → binding 1 (t1 = weights/initializer)
 *     uavBuffers[0] → binding 2 (u0 = output)
 *     uavBuffers[1] → binding 3 (u1 = scratch)
 */

/* Register one FfxApiResource.  A missing resource and a backend registration
 * failure are different conditions: mandatory dispatch inputs must propagate
 * either instead of becoming UINT32_MAX descriptors that silently skip. */
static FfxErrorCode reg_resource(Fsr4Context *c,
    const FfxApiResource *res, FfxUInt32 effectId, FfxResourceInternal *out)
{
    if (!c || !res || !res->resource || !out)
        return FFX_ERROR_INVALID_POINTER;
    out->internalIndex = UINT32_MAX;
    return c->iface.fpRegisterResource(&c->iface, res, effectId, out);
}

/* Sentinel for "no resource at this slot" — writeFullDs/writeModelDs skip these */
static const FfxResourceInternal RI_NONE = { UINT32_MAX };

ffxReturnCode_t ffxFsr4V07Dispatch(ffxContext *ctx, const ffxDispatchDescHeader *desc)
{
    if (!ctx || !*ctx || !desc) return FFX_API_RETURN_ERROR_PARAMETER;
    Fsr4Context *c = (Fsr4Context *)(*ctx);
    if (!c->valid || !c->resources_ok) return FFX_API_RETURN_ERROR;

    /* Find the upscale dispatch descriptor */
    const ffxDispatchDescUpscale *d = NULL;
    for (const ffxDispatchDescHeader *h = desc; h; h = h->pNext) {
        if (h->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE) {
            d = (const ffxDispatchDescUpscale *)h;
            break;
        }
    }
    if (!d) return FFX_API_RETURN_ERROR_PARAMETER;

    VkCommandBuffer cmd = (VkCommandBuffer)d->commandList;
    if (!cmd) return FFX_API_RETURN_ERROR_PARAMETER;
    uint32_t rw = d->renderSize.width;
    uint32_t rh = d->renderSize.height;
    uint32_t dw = d->upscaleSize.width  ? d->upscaleSize.width  : c->maxUpscaleSize.width;
    uint32_t dh = d->upscaleSize.height ? d->upscaleSize.height : c->maxUpscaleSize.height;
    FfxFsr4Dot4Schedule schedule;
    if (!rw || !rh || rw > c->maxRenderSize.width ||
        rh > c->maxRenderSize.height || dw > c->maxUpscaleSize.width ||
        dh > c->maxUpscaleSize.height ||
        !ffxFsr4BuildDot4Schedule(dw, dh, &schedule))
        return FFX_API_RETURN_ERROR_PARAMETER;

    /* Register external resources for this frame */
    FfxResourceInternal ri_color = RI_NONE;
    FfxResourceInternal ri_depth = RI_NONE;
    FfxResourceInternal ri_mv = RI_NONE;
    FfxResourceInternal ri_output = RI_NONE;
    FfxErrorCode register_error = reg_resource(
        c, &d->color, c->effectId, &ri_color);
    if (register_error == FFX_OK)
        register_error = reg_resource(c, &d->depth, c->effectId, &ri_depth);
    if (register_error == FFX_OK)
        register_error = reg_resource(
            c, &d->motionVectors, c->effectId, &ri_mv);
    if (register_error == FFX_OK)
        register_error = reg_resource(c, &d->output, c->effectId, &ri_output);
    if (register_error != FFX_OK) {
        /* Registration is append-only for the frame.  Always discard already
         * registered externals before surfacing the error. */
        c->iface.fpUnregisterResources(
            &c->iface, (FfxCommandList)cmd, c->effectId);
        return FFX_API_RETURN_ERROR;
    }

    /* Shorthand for internal resources */
    FfxResourceInternal ri_history     = c->ri_history;
    FfxResourceInternal ri_recurrent_r = c->ri_recurrent[c->recurrent_idx];      /* read  */
    FfxResourceInternal ri_recurrent_w = c->ri_recurrent[c->recurrent_idx ^ 1];  /* write */
    FfxResourceInternal ri_reprojected = c->ri_reprojected;
    FfxResourceInternal ri_rcas_temp   = c->ri_rcas_temp;
    FfxResourceInternal ri_exposure    = c->ri_exposure;
    FfxResourceInternal ri_spd_atomic  = c->ri_spd_atomic;
    FfxResourceInternal ri_spd_mip5    = c->ri_spd_mip5;
    FfxResourceInternal ri_scratch     = c->ri_scratch;
    FfxResourceInternal ri_initializer = c->ri_initializer;

    /* Build the generated OptimizedConstants UBO (104 bytes). */
    struct {
        float    inv_size[2];
        float    scale[2];
        float    inv_scale[2];
        float    jitter[2];
        float    mv_scale[2];
        float    tex_size[2];
        float    max_renderSize[2];
        float    fMotionVectorJitterCancellation[2];
        uint32_t width;
        uint32_t height;
        uint32_t reset;
        uint32_t width_lr;
        uint32_t height_lr;
        float    preExposure;
        float    previous_preExposure;
        uint32_t rcas_enabled;
        float    rcas_sharpness;
        float    _pad1;
    } cb;
    memset(&cb, 0, sizeof(cb));
    cb.inv_size[0]    = 1.0f / (float)dw;
    cb.inv_size[1]    = 1.0f / (float)dh;
    cb.scale[0]       = (float)dw / (float)rw;
    cb.scale[1]       = (float)dh / (float)rh;
    cb.inv_scale[0]   = (float)rw / (float)dw;
    cb.inv_scale[1]   = (float)rh / (float)dh;
    cb.jitter[0]      = d->jitterOffset.x;
    cb.jitter[1]      = d->jitterOffset.y;
    cb.mv_scale[0]    = d->motionVectorScale.x / (float)rw;
    cb.mv_scale[1]    = d->motionVectorScale.y / (float)rh;
    cb.tex_size[0]    = (float)c->maxUpscaleSize.width;
    cb.tex_size[1]    = (float)c->maxUpscaleSize.height;
    cb.max_renderSize[0] = (float)c->maxRenderSize.width;
    cb.max_renderSize[1] = (float)c->maxRenderSize.height;
    cb.width          = schedule.alignedWidth;
    cb.height         = schedule.alignedHeight;
    cb.reset          = d->reset ? 1u : 0u;
    cb.width_lr       = rw;
    cb.height_lr      = rh;
    cb.preExposure    = (d->preExposure > 0.0f) ? d->preExposure : 1.0f;
    cb.previous_preExposure = c->previous_pre_exposure;
    cb.rcas_enabled   = d->enableSharpening ? 1u : 0u;
    cb.rcas_sharpness = d->sharpness;

    /* Stage the cbuffer */
    FfxConstantBuffer cbHandle = {{0}};
    FfxConstantBuffer weightsHandle = {{0}};
    size_t weights_size = 0;
    const void *weights = ffxFsr4VkGetPrePassWeights(&c->iface, &weights_size);
    if (!weights || weights_size != 1024u ||
        c->iface.fpStageConstantBufferDataFunc(&c->iface, &cb,
            (FfxUInt32)sizeof(cb), &cbHandle) != FFX_OK ||
        c->iface.fpStageConstantBufferDataFunc(&c->iface, (void *)weights,
            (FfxUInt32)weights_size, &weightsHandle) != FFX_OK)
        goto dispatch_error;

    if (d->enableAutoExposure) {
        struct {
            uint32_t mips;
            uint32_t numWorkGroups;
            uint32_t workGroupOffset[2];
            float invInputSize[2];
            float preExposure;
            float padding;
        } spd_cb;
        FfxConstantBuffer spd_handle = {{0}};
        FfxGpuJobDescription job;
        FfxComputeJobDescription *cj;
        FfxFsr4SpdSchedule spd_schedule;

        if (!ffxFsr4BuildSpdSchedule(rw, rh, &spd_schedule))
            goto dispatch_error;
        memset(&spd_cb, 0, sizeof(spd_cb));
        spd_cb.mips = spd_schedule.mipCount;
        spd_cb.numWorkGroups = spd_schedule.workgroupCount;
        spd_cb.invInputSize[0] = 1.0f / (float)rw;
        spd_cb.invInputSize[1] = 1.0f / (float)rh;
        spd_cb.preExposure = cb.preExposure;
        if (c->iface.fpStageConstantBufferDataFunc(&c->iface, &spd_cb,
                (FfxUInt32)sizeof(spd_cb), &spd_handle) != FFX_OK)
            goto dispatch_error;

        memset(&job, 0, sizeof(job));
        job.jobType = FFX_GPU_JOB_COMPUTE;
        cj = &job.computeJobDescriptor;
        cj->pipeline.pipeline = (FfxPipeline)(uintptr_t)16u; /* pass 15 + 1 */
        cj->pipeline.srvTextureCount = 1; /* t0: r_input_color */
        cj->srvTextures[0].resource = ri_color;
        cj->pipeline.uavTextureCount = 3; /* u0..u2 */
        cj->uavTextures[0].resource = ri_spd_atomic;
        cj->uavTextures[1].resource = ri_spd_mip5;
        cj->uavTextures[2].resource = ri_exposure;
        cj->pipeline.constCount = 1;
        cj->cbs[0] = spd_handle;
        cj->dimensions[0] = spd_schedule.dispatch.x;
        cj->dimensions[1] = spd_schedule.dispatch.y;
        cj->dimensions[2] = 1;
        if (c->iface.fpScheduleGpuJob(&c->iface, &job) != FFX_OK)
            goto dispatch_error;
    } else if (c->auto_exposure_last_enabled) {
        /* Switching back to explicit exposure must not retain the last SPD
         * value: the explicit path's contract is a sampled identity of 1. */
        FfxGpuJobDescription clear_job;
        memset(&clear_job, 0, sizeof(clear_job));
        clear_job.jobType = FFX_GPU_JOB_CLEAR_FLOAT;
        clear_job.clearJobDescriptor.target = ri_exposure;
        clear_job.clearJobDescriptor.color[0] = 1.0f;
        if (c->iface.fpScheduleGpuJob(&c->iface, &clear_job) != FFX_OK)
            goto dispatch_error;
    }

    /* ── Schedule all 14 compute passes ──────────────────────────────────── */
    for (int pass = 0; pass <= 13; pass++) {

        FfxGpuJobDescription job;
        memset(&job, 0, sizeof(job));
        job.jobType = FFX_GPU_JOB_COMPUTE;

        FfxComputeJobDescription *cj = &job.computeJobDescriptor;

        /* Backend handles encode pass+1 so PRE (pass 0) is not confused with
         * a missing/null pipeline handle. */
        cj->pipeline.pipeline =
            (FfxPipeline)(uintptr_t)((uint32_t)pass + 1u);

        if (pass == 0) {
            /* ── PRE-PASS ────────────────────────────────────────────────
             * SRV textures indexed by t-register (writeFullDs uses SLOT_SRV_TEX_BASE+i):
             *   t0 = r_history_color     (previous upscaled output)
             *   t1 = r_velocity          (motion vectors)
             *   t3 = r_input_color       (current frame)
             *   t4 = r_recurrent_0       (recurrent feature map, read)
             *   t6 = r_input_exposure    (exposure)
             * UAV textures indexed by u-register (writeFullDs uses SLOT_UAV_BASE+i):
             *   u3 = rw_reprojected_color
             * UAV buffers (via bindingIndex):
             *   u11 = ScratchBuffer
             * CBV:
             *   b0 = OptimizedConstants
             */
            cj->pipeline.srvTextureCount = 7;  /* t0..t6, pad unused */
            cj->srvTextures[0].resource = ri_history;       /* t0 */
            cj->srvTextures[1].resource = ri_mv;            /* t1 */
            cj->srvTextures[2].resource = ri_depth;         /* t2: low-res depth */
            cj->srvTextures[3].resource = ri_color;         /* t3 */
            cj->srvTextures[4].resource = ri_recurrent_r;   /* t4 */
            cj->srvTextures[5].resource = RI_NONE;          /* t5 unused */
            cj->srvTextures[6].resource = ri_exposure;      /* t6 */

            cj->pipeline.uavTextureCount = 4;  /* u0..u3, pad unused */
            cj->uavTextures[0].resource = RI_NONE;          /* u0 unused */
            cj->uavTextures[1].resource = RI_NONE;          /* u1 unused */
            cj->uavTextures[2].resource = RI_NONE;          /* u2 unused */
            cj->uavTextures[3].resource = ri_reprojected;   /* u3 → bind 24 */

            /* ScratchBuffer at u11 */
            cj->pipeline.uavBufferCount = 1;
            cj->uavBuffers[0].resource = ri_scratch;
            cj->pipeline.uavBufferBindings[0].bindingIndex = 11;  /* u11 → bind 32 */

            cj->pipeline.constCount = 2;
            cj->cbs[0] = cbHandle;
            cj->cbs[1] = weightsHandle;

            cj->dimensions[0] = schedule.pre.x;
            cj->dimensions[1] = schedule.pre.y;
            cj->dimensions[2] = schedule.pre.z;

        } else if (pass == 13) {
            /* ── POST-PASS ───────────────────────────────────────────────
             *   t3 = r_input_color
             *   t6 = r_input_exposure
             *   t9 = r_reprojected_color  (from pre-pass)
             *   u1 = rw_history_color     (write history for next frame)
             *   u2 = rw_mlsr_output_color (final output)
             *   u6 = rw_recurrent_0       (write recurrent for next frame)
             *   u11 = ScratchBuffer
             *   b0 = OptimizedConstants
             */
            cj->pipeline.srvTextureCount = 10;  /* t0..t9, pad unused */
            cj->srvTextures[0].resource = RI_NONE;          /* t0 unused */
            cj->srvTextures[1].resource = RI_NONE;          /* t1 unused */
            cj->srvTextures[2].resource = RI_NONE;          /* t2 unused */
            cj->srvTextures[3].resource = ri_color;         /* t3 */
            cj->srvTextures[4].resource = RI_NONE;          /* t4 unused */
            cj->srvTextures[5].resource = RI_NONE;          /* t5 unused */
            cj->srvTextures[6].resource = ri_exposure;      /* t6 */
            cj->srvTextures[7].resource = RI_NONE;          /* t7 unused */
            cj->srvTextures[8].resource = RI_NONE;          /* t8 unused */
            cj->srvTextures[9].resource = ri_reprojected;   /* t9 */

            cj->pipeline.uavTextureCount = 7;  /* u0..u6, pad unused */
            cj->uavTextures[0].resource = RI_NONE;          /* u0 unused */
            cj->uavTextures[1].resource = ri_history;       /* u1 → bind 22 (write) */
            cj->uavTextures[2].resource = d->enableSharpening
                ? ri_rcas_temp : ri_output;                  /* u2 → bind 23 */
            cj->uavTextures[3].resource = RI_NONE;          /* u3 unused */
            cj->uavTextures[4].resource = RI_NONE;          /* u4 unused */
            cj->uavTextures[5].resource = RI_NONE;          /* u5 unused */
            cj->uavTextures[6].resource = ri_recurrent_w;   /* u6 → bind 27 (write) */

            /* ScratchBuffer at u11 */
            cj->pipeline.uavBufferCount = 1;
            cj->uavBuffers[0].resource = ri_scratch;
            cj->pipeline.uavBufferBindings[0].bindingIndex = 11;  /* u11 → bind 32 */

            cj->pipeline.constCount = 1;
            cj->cbs[0] = cbHandle;

            cj->dimensions[0] = schedule.post.x;
            cj->dimensions[1] = schedule.post.y;
            cj->dimensions[2] = schedule.post.z;

        } else {
            /* ── MODEL PASSES 1-12 ───────────────────────────────────────
             * Compiled with -fvk-t-shift 0 -fvk-u-shift 2:
             *   bind 0 (t0): input scratch buffer  → srvBuffers[0]
             *   bind 1 (t1): weights/initializer   → srvBuffers[1]
             *   bind 2 (u0): output scratch buffer → uavBuffers[0]
             *   bind 3 (u1): scratch/aux buffer    → uavBuffers[1]
             *
             * Scratch is aliased for input/output/auxiliary activations.  The
             * initializer is a separate immutable model parameter buffer.
             */
            cj->pipeline.srvBufferCount = 2;
            cj->pipeline.uavBufferCount = 2;
            cj->srvBuffers[0].resource = ri_scratch;   /* bind 0: input */
            cj->srvBuffers[1].resource = ri_initializer; /* bind 1: model data */
            cj->uavBuffers[0].resource = ri_scratch;   /* bind 2: output */
            cj->uavBuffers[1].resource = ri_scratch;   /* bind 3: scratch */

            cj->dimensions[0] = schedule.model[pass - 1].x;
            cj->dimensions[1] = schedule.model[pass - 1].y;
            cj->dimensions[2] = schedule.model[pass - 1].z;
        }

        if (c->iface.fpScheduleGpuJob(&c->iface, &job) != FFX_OK)
            goto dispatch_error;
    }

    if (d->enableSharpening) {
        /* Exact FSR RCAS cbuffer ABI: uint4 config, pre-exposure, uint3 pad.
         * The API amount is 0..1, while RCAS uses stops with 0 = strongest,
         * hence AMD's 2 - 2*amount remap. */
        struct {
            uint32_t config[4];
            float preExposure;
            uint32_t pad[3];
        } rcas_cb;
        const float amount = fminf(fmaxf(d->sharpness, 0.0f), 1.0f);
        const float rcas_stops = 2.0f - 2.0f * amount;
        const float rcas_linear = exp2f(-rcas_stops);
        FfxConstantBuffer rcas_handle = {{0}};
        FfxGpuJobDescription job;
        FfxComputeJobDescription *cj;

        memset(&rcas_cb, 0, sizeof(rcas_cb));
        memcpy(&rcas_cb.config[0], &rcas_linear, sizeof(rcas_linear));
        rcas_cb.config[1] = (uint32_t)float_to_half(rcas_linear) |
                            ((uint32_t)float_to_half(rcas_linear) << 16u);
        rcas_cb.preExposure = cb.preExposure;
        if (c->iface.fpStageConstantBufferDataFunc(&c->iface, &rcas_cb,
                (FfxUInt32)sizeof(rcas_cb), &rcas_handle) != FFX_OK)
            goto dispatch_error;

        memset(&job, 0, sizeof(job));
        job.jobType = FFX_GPU_JOB_COMPUTE;
        cj = &job.computeJobDescriptor;
        cj->pipeline.pipeline = (FfxPipeline)(uintptr_t)15u; /* pass 14 + 1 */
        cj->pipeline.srvTextureCount = 19; /* t0..t18 */
        cj->pipeline.uavTextureCount = 12; /* u0..u11 */
        for (uint32_t i = 0; i < cj->pipeline.srvTextureCount; ++i)
            cj->srvTextures[i].resource = RI_NONE;
        for (uint32_t i = 0; i < cj->pipeline.uavTextureCount; ++i)
            cj->uavTextures[i].resource = RI_NONE;
        cj->srvTextures[6].resource = ri_exposure;   /* t6 */
        cj->srvTextures[18].resource = ri_rcas_temp; /* t18 */
        cj->uavTextures[11].resource = ri_output;    /* u11 */
        cj->pipeline.constCount = 1;
        cj->cbs[0] = rcas_handle;
        cj->dimensions[0] = schedule.rcas.x;
        cj->dimensions[1] = schedule.rcas.y;
        cj->dimensions[2] = schedule.rcas.z;
        if (c->iface.fpScheduleGpuJob(&c->iface, &job) != FFX_OK)
            goto dispatch_error;
    }

    /* ── Execute all jobs ──────────────────────────────────────────────────── */
    if (c->iface.fpExecuteGpuJobs(&c->iface, (FfxCommandList)cmd,
                                  c->effectId) != FFX_OK)
        goto dispatch_error;

    /* ── Unregister per-frame external resources ──────────────────────────── */
    if (c->iface.fpUnregisterResources(
            &c->iface, (FfxCommandList)cmd, c->effectId) != FFX_OK)
        return FFX_API_RETURN_ERROR;

    /* ── Swap recurrent ping-pong for next frame ─────────────────────────── */
    c->previous_pre_exposure = cb.preExposure;
    c->auto_exposure_last_enabled = d->enableAutoExposure;
    c->recurrent_idx ^= 1;

    return FFX_API_RETURN_OK;

dispatch_error:
    c->iface.fpUnregisterResources(
        &c->iface, (FfxCommandList)cmd, c->effectId);
    return FFX_API_RETURN_ERROR;
}

int
ffxFsr4GetDebugResource(ffxContext *ctx, FfxFsr4DebugResource resource,
    FfxApiResource *out_resource)
{
    Fsr4Context *c;
    FfxResourceInternal internal;

    if (!ctx || !*ctx || !out_resource)
        return 0;
    c = (Fsr4Context *)(*ctx);
    if (!c->valid || !c->resources_ok || !c->iface.fpGetResource ||
        !c->iface.fpGetResourceDescription)
        return 0;
    switch (resource) {
    case FFX_FSR4_DEBUG_RESOURCE_HISTORY:
        internal = c->ri_history;
        break;
    case FFX_FSR4_DEBUG_RESOURCE_REPROJECTED:
        internal = c->ri_reprojected;
        break;
    default:
        return 0;
    }
    *out_resource = c->iface.fpGetResource(&c->iface, internal);
    out_resource->description = c->iface.fpGetResourceDescription(
        &c->iface, internal);
    return out_resource->resource && out_resource->description.width &&
        out_resource->description.height;
}

/* ── ffxConfigure (stub — not needed for basic upscaling) ─────────────────── */

ffxReturnCode_t ffxFsr4V07Configure(ffxContext *ctx, const ffxApiHeader *desc)
{
    (void)ctx; (void)desc;
    return FFX_API_RETURN_OK;
}
