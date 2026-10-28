#include "scenes/orman/orman_dlss.h"

#if defined(ORMAN_DLSS)

#include <windows.h>

#include <nvsdk_ngx_helpers_dlssd_cuda.h>
#include <nvsdk_ngx_params.h>

#include <cstdio>
#include <filesystem>

namespace {

// a project id of our own (NGX asks custom engines for any UUID)
const char* kProject = "3f1c9a52-6e0d-4b7a-9c41-0b2f5d8e7a11";

std::string what(NVSDK_NGX_Result r) {
    char b[64];
    std::snprintf(b, sizeof(b), "NGX 0x%08x", unsigned(r));
    return b;
}

std::wstring exeDir() {
    wchar_t p[2048] = {0};
    if (GetModuleFileNameW(nullptr, p, 2048) == 0) return L".";
    return std::filesystem::path(p).parent_path().wstring();
}

}   // namespace

bool OrmanDlss::init(int width, int height, CUstream stream) {
    release();
    error.clear();
    const std::wstring dir = exeDir();
    NVSDK_NGX_Result r = NVSDK_NGX_CUDA_Init_with_ProjectID(kProject, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", dir.c_str());
    if (NVSDK_NGX_FAILED(r)) {
        error = "NGX acilmadi (" + what(r) + ")";
        return false;
    }
    ngx_ = true;
    NVSDK_NGX_Parameter* p = nullptr;
    r = NVSDK_NGX_CUDA_GetCapabilityParameters(&p);
    if (NVSDK_NGX_FAILED(r) || !p) {
        error = "NGX yetenekleri okunamadi (" + what(r) + ")";
        release();
        return false;
    }
    params_ = p;
    int available = 0, oldDriver = 0;
    NVSDK_NGX_Parameter_GetI(p, NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &available);
    NVSDK_NGX_Parameter_GetI(p, NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver, &oldDriver);
    if (!available) {
        error = oldDriver ? "DLSS-RR icin surucu eski" : "DLSS-RR bu kartta yok";
        release();
        return false;
    }
    NVSDK_NGX_DLSSD_Create_Params c{};
    c.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
    c.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Unpacked;
    c.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_Linear;
    c.InWidth = unsigned(width);
    c.InHeight = unsigned(height);
    c.InTargetWidth = unsigned(width);
    c.InTargetHeight = unsigned(height);
    c.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
    // linear light, motion vectors at the render's own size and without the jitter
    c.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_FreeMemOnReleaseFeature, 1);
    CUcontext ctx = nullptr;
    cuCtxGetCurrent(&ctx);
    NVSDK_NGX_CUDA_DLSSD_Create_Params cc{};
    cc.Feature = c;
    cc.InCUContext = ctx;
    cc.InCUStream = stream;
    NVSDK_NGX_Handle* h = nullptr;
    r = NGX_CUDA_CREATE_DLSSD_EXT(&h, p, &cc);
    if (NVSDK_NGX_FAILED(r) || !h) {
        error = "DLSS-RR kurulamadi (" + what(r) + ")";
        release();
        return false;
    }
    handle_ = h;
    w_ = unsigned(width);
    h_ = unsigned(height);
    return true;
}

bool OrmanDlss::evaluate(const OrmanDlssFrame& f) {
    if (!handle_) return false;
    // NGX takes pointers to the CUDA objects (textures for the inputs, a surface for the output)
    CUtexObject color = f.color, albedo = f.albedo, specular = f.specular, normal = f.normal, rough = f.rough, depth = f.depth, motion = f.motion,
                hitT = f.hitT;
    CUsurfObject out = f.output;
    float w2v[16], v2c[16];
    for (int k = 0; k < 16; ++k) {
        w2v[k] = f.worldToView[k];
        v2c[k] = f.viewToClip[k];
    }
    NVSDK_NGX_CUDA_DLSSD_Eval_Params e{};
    e.pInColor = &color;
    e.pInOutput = &out;
    e.pInDiffuseAlbedo = &albedo;
    e.pInSpecularAlbedo = &specular;
    e.pInNormals = &normal;
    e.pInRoughness = &rough;
    e.pInDepth = &depth;
    e.pInMotionVectors = &motion;
    e.pInSpecularHitDistance = &hitT;
    e.pInWorldToViewMatrix = w2v;
    e.pInViewToClipMatrix = v2c;
    e.InJitterOffsetX = f.jitterX;
    e.InJitterOffsetY = f.jitterY;
    e.InReset = f.reset ? 1 : 0;
    e.InMVScaleX = 1.0f;
    e.InMVScaleY = 1.0f;
    e.InFrameTimeDeltaInMsec = f.frameMs;
    e.InPreExposure = f.preExposure;
    e.InRenderSubrectDimensions = {w_, h_};
    const NVSDK_NGX_Result r = NGX_CUDA_EVALUATE_DLSSD_EXT(static_cast<NVSDK_NGX_Handle*>(handle_), static_cast<NVSDK_NGX_Parameter*>(params_), &e);
    if (NVSDK_NGX_FAILED(r)) {
        error = "DLSS-RR calismadi (" + what(r) + ")";
        return false;
    }
    return true;
}

void OrmanDlss::release() {
    if (handle_) NVSDK_NGX_CUDA_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(handle_));
    handle_ = nullptr;
    if (params_) NVSDK_NGX_CUDA_DestroyParameters(static_cast<NVSDK_NGX_Parameter*>(params_));
    params_ = nullptr;
    if (ngx_) NVSDK_NGX_CUDA_Shutdown();
    ngx_ = false;
}

#else

bool OrmanDlss::init(int, int, CUstream) {
    error = "DLSS yok (third_party/fetch.sh ile third_party/dlss)";
    return false;
}
bool OrmanDlss::evaluate(const OrmanDlssFrame&) { return false; }
void OrmanDlss::release() {}

#endif
