// Scene 7 "Ölçek": loading the maps (see olcek_tex.h).
#include "scenes/olcek/olcek_tex.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace olcek {

bool Texture::load(const std::string& path, bool wrapU, std::string& err) {
    release();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "yok: " + path;
        return false;
    }
    char magic[4];
    uint32_t hdr[3];
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "OLTX", 4) != 0 || std::fread(hdr, 4, 3, f) != 3) {
        std::fclose(f);
        err = "bicim: " + path;
        return false;
    }
    width = int(hdr[0]);
    height = int(hdr[1]);
    levels = int(hdr[2]);
    const cudaChannelFormatDesc fmt = cudaCreateChannelDesc<uchar4>();
    if (cudaMallocMipmappedArray(&arr, &fmt, make_cudaExtent(width, height, 0), levels) != cudaSuccess) {
        std::fclose(f);
        err = "kart bellegi: " + path;
        arr = nullptr;
        return false;
    }
    std::vector<unsigned char> buf;
    int w = width, h = height;
    bytes = 0;
    for (int l = 0; l < levels; ++l) {
        const size_t n = size_t(w) * h * 4;
        buf.resize(n);
        if (std::fread(buf.data(), 1, n, f) != n) {
            std::fclose(f);
            err = "kisa: " + path;
            release();
            return false;
        }
        cudaArray_t a;
        cudaGetMipmappedArrayLevel(&a, arr, l);
        cudaMemcpy2DToArray(a, 0, 0, buf.data(), size_t(w) * 4, size_t(w) * 4, h, cudaMemcpyHostToDevice);
        bytes += n;
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    std::fclose(f);
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeMipmappedArray;
    rd.res.mipmap.mipmap = arr;
    cudaTextureDesc td{};
    td.addressMode[0] = wrapU ? cudaAddressModeWrap : cudaAddressModeClamp;
    td.addressMode[1] = cudaAddressModeClamp;
    td.filterMode = cudaFilterModeLinear;
    td.mipmapFilterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeNormalizedFloat;
    td.sRGB = 1;
    td.normalizedCoords = 1;
    td.maxMipmapLevelClamp = float(levels - 1);
    td.maxAnisotropy = 1;
    if (cudaCreateTextureObject(&tex, &rd, &td, nullptr) != cudaSuccess) {
        err = "doku nesnesi: " + path;
        release();
        return false;
    }
    return true;
}

void Texture::release() {
    if (tex) cudaDestroyTextureObject(tex);
    if (arr) cudaFreeMipmappedArray(arr);
    tex = 0;
    arr = nullptr;
    width = height = levels = 0;
    bytes = 0;
}

}   // namespace olcek
