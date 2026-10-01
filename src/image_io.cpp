#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
#define TINYEXR_USE_STB_ZLIB 0
#include "miniz.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "tinyexr.h"

#include "image_io.h"

bool writePNG(const std::string& path, int w, int h, const unsigned char* rgba) {
    return stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) != 0;
}

bool writeEXR(const std::string& path, int w, int h, int channels, const float* data) {
    const char* err = nullptr;
    const int r = SaveEXR(data, w, h, channels, 0 /* full float */, path.c_str(), &err);
    if (err) FreeEXRErrorMessage(err);
    return r == TINYEXR_SUCCESS;
}

bool readPNGGray(const std::string& path, int& w, int& h, std::vector<unsigned char>& out) {
    int n = 0;
    unsigned char* p = stbi_load(path.c_str(), &w, &h, &n, 1);
    if (!p) return false;
    out.assign(p, p + size_t(w) * h);
    stbi_image_free(p);
    return true;
}

bool readEXR(const std::string& path, int& w, int& h, std::vector<float>& rgba) {
    float* out = nullptr;
    const char* err = nullptr;
    if (LoadEXR(&out, &w, &h, path.c_str(), &err) != TINYEXR_SUCCESS) {
        if (err) FreeEXRErrorMessage(err);
        return false;
    }
    rgba.assign(out, out + size_t(w) * h * 4);
    free(out);
    return true;
}
