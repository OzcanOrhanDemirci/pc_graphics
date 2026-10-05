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

bool Field::load(const std::string& path, std::string& err) {
    release();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "yok: " + path;
        return false;
    }
    char magic[4];
    uint32_t n = 0;
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "OLSD", 4) != 0 || std::fread(&n, 4, 1, f) != 1 ||
        std::fread(&texel, 4, 1, f) != 1) {
        std::fclose(f);
        err = "bicim: " + path;
        return false;
    }
    size = int(n);
    std::vector<float> v(size_t(size) * size);
    if (std::fread(v.data(), 4, v.size(), f) != v.size()) {
        std::fclose(f);
        err = "kisa: " + path;
        return false;
    }
    std::fclose(f);
    // the middle: the mean of the four texels around it (the beach point is their shared corner)
    const int c = size / 2;
    centre = 0.25f * (v[size_t(c - 1) * size + c - 1] + v[size_t(c - 1) * size + c] + v[size_t(c) * size + c - 1] + v[size_t(c) * size + c]);
    const cudaChannelFormatDesc fmt = cudaCreateChannelDesc<float>();
    if (cudaMallocArray(&arr, &fmt, size, size) != cudaSuccess) {
        err = "kart bellegi: " + path;
        arr = nullptr;
        return false;
    }
    cudaMemcpy2DToArray(arr, 0, 0, v.data(), size_t(size) * 4, size_t(size) * 4, size, cudaMemcpyHostToDevice);
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeArray;
    rd.res.array.array = arr;
    cudaTextureDesc td{};
    td.addressMode[0] = td.addressMode[1] = cudaAddressModeClamp;
    // point samples: the shader interpolates in float (the hardware's bilinear weights have 8 bits: a 2 m texel seen at
    // 1 m across the screen showed 8 mm steps)
    td.filterMode = cudaFilterModePoint;
    td.readMode = cudaReadModeElementType;
    td.normalizedCoords = 0;
    if (cudaCreateTextureObject(&tex, &rd, &td, nullptr) != cudaSuccess) {
        err = "doku nesnesi: " + path;
        release();
        return false;
    }
    return true;
}

void Field::release() {
    if (tex) cudaDestroyTextureObject(tex);
    if (arr) cudaFreeArray(arr);
    tex = 0;
    arr = nullptr;
    size = 0;
}

}   // namespace olcek
