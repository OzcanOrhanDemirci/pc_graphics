#include "scenes/orman/orman_assets.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float linearToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

struct Level {
    int w, h;
    std::vector<unsigned char> px;   // channels per texel: 4, or 1 for alpha
};

// half size, 2 x 2 box (an odd edge keeps its last texel)
template <class F> Level halve(const Level& a, int ch, F avg) {
    Level b;
    b.w = std::max(1, a.w / 2);
    b.h = std::max(1, a.h / 2);
    b.px.resize(size_t(b.w) * b.h * ch);
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x) {
            const int x0 = std::min(2 * x, a.w - 1), x1 = std::min(2 * x + 1, a.w - 1);
            const int y0 = std::min(2 * y, a.h - 1), y1 = std::min(2 * y + 1, a.h - 1);
            const unsigned char* q[4] = {&a.px[(size_t(y0) * a.w + x0) * ch], &a.px[(size_t(y0) * a.w + x1) * ch],
                                         &a.px[(size_t(y1) * a.w + x0) * ch], &a.px[(size_t(y1) * a.w + x1) * ch]};
            avg(q, &b.px[(size_t(y) * b.w + x) * ch]);
        }
    return b;
}

float coverage(const Level& l, float cutoff, float scale) {
    size_t n = 0;
    const float c = cutoff * 255.0f;
    for (unsigned char v : l.px)
        if (float(v) * scale >= c) ++n;
    return float(n) / float(std::max<size_t>(l.px.size(), 1));
}

std::string findFile(const std::string& dir, const std::vector<std::string>& keys, const std::string& notKey = "") {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};
    for (const std::string& k : keys)
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            const std::string name = e.path().filename().string();
            std::string low = name;
            std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            std::string kl = k;
            std::transform(kl.begin(), kl.end(), kl.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            const std::string ext = e.path().extension().string();
            if (ext != ".jpg" && ext != ".png" && ext != ".jpeg" && ext != ".gltf") continue;
            if (low.find(kl) != std::string::npos && (notKey.empty() || low.find(notKey) == std::string::npos))
                return e.path().generic_string();
        }
    return {};
}


// a mip chain to a GPU texture: wrapped, trilinear, read as 0..1 (sRGB decoded for colour)
bool uploadLevels(const std::vector<Level>& levels, int ch, bool srgb, OTexture& t) {
    t.w = levels[0].w;
    t.h = levels[0].h;
    cudaChannelFormatDesc fd = ch == 4 ? cudaCreateChannelDesc<uchar4>() : cudaCreateChannelDesc<unsigned char>();
    if (cudaMallocMipmappedArray(&t.arr, &fd, make_cudaExtent(t.w, t.h, 0), unsigned(levels.size())) != cudaSuccess) {
        cudaGetLastError();
        return false;
    }
    for (size_t k = 0; k < levels.size(); ++k) {
        cudaArray_t la = nullptr;
        cudaGetMipmappedArrayLevel(&la, t.arr, unsigned(k));
        const Level& l = levels[k];
        cudaMemcpy2DToArray(la, 0, 0, l.px.data(), size_t(l.w) * ch, size_t(l.w) * ch, l.h, cudaMemcpyHostToDevice);
        t.bytes += l.px.size();
    }
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeMipmappedArray;
    rd.res.mipmap.mipmap = t.arr;
    cudaTextureDesc td{};
    td.addressMode[0] = td.addressMode[1] = cudaAddressModeWrap;
    td.filterMode = cudaFilterModeLinear;
    td.mipmapFilterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeNormalizedFloat;
    td.normalizedCoords = 1;
    td.sRGB = srgb ? 1 : 0;
    td.maxMipmapLevelClamp = float(levels.size() - 1);
    td.minMipmapLevelClamp = 0.0f;
    return cudaCreateTextureObject(&t.tex, &rd, &td, nullptr) == cudaSuccess;
}

void chain(std::vector<Level>& levels, int ch) {
    while (levels.back().w > 1 || levels.back().h > 1)
        levels.push_back(halve(levels.back(), ch, [ch](const unsigned char* const* q, unsigned char* o) {
            for (int c = 0; c < ch; ++c) o[c] = (unsigned char)((q[0][c] + q[1][c] + q[2][c] + q[3][c] + 2) / 4);
        }));
}

}   // namespace

int OrmanAssets::texture(const std::string& path, Kind kind, float cutoff, bool alphaChannel) {
    const std::string key = path + "|" + std::to_string(int(kind)) + (alphaChannel ? "|a" : "");
    auto it = texIndex_.find(key);
    if (it != texIndex_.end()) return it->second;
    int w = 0, h = 0, n = 0;
    const int ch = kind == Alpha ? 1 : 4;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, alphaChannel ? 4 : ch);
    if (!data) {
        std::fprintf(stderr, "orman: doku okunamadi %s\n", path.c_str());
        texIndex_[key] = -1;
        return -1;
    }
    std::vector<Level> levels(1);
    levels[0].w = w;
    levels[0].h = h;
    levels[0].px.resize(size_t(w) * h * ch);
    if (alphaChannel)
        for (size_t i = 0; i < size_t(w) * h; ++i) levels[0].px[i] = data[i * 4 + 3];
    else
        std::memcpy(levels[0].px.data(), data, levels[0].px.size());
    stbi_image_free(data);

    static float lin[256];
    static bool linReady = false;
    if (!linReady) {
        for (int i = 0; i < 256; ++i) lin[i] = srgbToLinear(i / 255.0f);
        linReady = true;
    }
    while (levels.back().w > 1 || levels.back().h > 1) {
        const Level& a = levels.back();
        Level b;
        if (kind == Albedo) {
            // in linear light: an sRGB average darkens the mips (the far ground would turn muddy)
            b = halve(a, 4, [&](const unsigned char* const* q, unsigned char* o) {
                for (int c = 0; c < 3; ++c) o[c] = (unsigned char)std::lround(linearToSrgb(0.25f * (lin[q[0][c]] + lin[q[1][c]] + lin[q[2][c]] + lin[q[3][c]])) * 255.0f);
                o[3] = (unsigned char)((q[0][3] + q[1][3] + q[2][3] + q[3][3] + 2) / 4);
            });
        } else if (kind == Normal) {
            b = halve(a, 4, [](const unsigned char* const* q, unsigned char* o) {
                float v[3] = {0, 0, 0};
                for (int k = 0; k < 4; ++k)
                    for (int c = 0; c < 3; ++c) v[c] += q[k][c] / 127.5f - 1.0f;
                const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                for (int c = 0; c < 3; ++c) o[c] = (unsigned char)std::lround(std::clamp((l > 1e-6f ? v[c] / l : (c == 2 ? 1.0f : 0.0f)) * 127.5f + 127.5f, 0.0f, 255.0f));
                o[3] = 255;
            });
        } else {
            b = halve(a, ch, [ch](const unsigned char* const* q, unsigned char* o) {
                for (int c = 0; c < ch; ++c) o[c] = (unsigned char)((q[0][c] + q[1][c] + q[2][c] + q[3][c] + 2) / 4);
            });
        }
        levels.push_back(std::move(b));
    }
    if (kind == Alpha) {
        // the coverage kept: every level scaled so the share over the cutoff is level 0's
        const float c0 = coverage(levels[0], cutoff, 1.0f);
        for (size_t k = 1; k < levels.size(); ++k) {
            float lo = 0.0f, hi = 8.0f;
            for (int it = 0; it < 16; ++it) {
                const float mid = 0.5f * (lo + hi);
                if (coverage(levels[k], cutoff, mid) < c0) lo = mid;
                else hi = mid;
            }
            for (unsigned char& v : levels[k].px) v = (unsigned char)std::min(255.0f, std::round(float(v) * hi));
        }
    }

    OTexture t;
    if (!uploadLevels(levels, ch, kind == Albedo, t)) {
        std::fprintf(stderr, "orman: doku icin bellek yok %s\n", path.c_str());
        texIndex_[key] = -1;
        return -1;
    }
    textures.push_back(t);
    const int idx = int(textures.size()) - 1;
    texIndex_[key] = idx;
    return idx;
}

int OrmanAssets::addMaterial(const OMaterial& m, const std::string& name) {
    materials.push_back(m);
    materialNames.push_back(name);
    return int(materials.size()) - 1;
}

bool OrmanAssets::loadModel(const std::string& id, bool solid) {
    const std::string dir = root + "/" + id;
    const std::string path = findFile(dir, {".gltf"});
    if (path.empty()) {
        missing.push_back(id);
        return false;
    }
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success || cgltf_load_buffers(&opt, data, path.c_str()) != cgltf_result_success) {
        if (data) cgltf_free(data);
        std::fprintf(stderr, "orman: glTF okunamadi %s\n", path.c_str());
        missing.push_back(id);
        return false;
    }
    const std::string base = fs::path(path).parent_path().generic_string();
    std::map<const cgltf_material*, int> matIndex;
    auto imagePath = [&](const cgltf_texture_view& v) -> std::string {
        if (!v.texture || !v.texture->image || !v.texture->image->uri) return {};
        return base + "/" + v.texture->image->uri;
    };
    auto material = [&](const cgltf_material* gm) -> int {
        auto it = matIndex.find(gm);
        if (it != matIndex.end()) return it->second;
        OMaterial m{};
        m.tint = make_float3(1, 1, 1);
        m.roughness = 0.8f;
        m.alphaCutoff = 0.5f;
        m.translucency = 0.0f;
        std::string name = id;
        if (gm) {
            if (gm->name) name += "/" + std::string(gm->name);
            const auto& pbr = gm->pbr_metallic_roughness;
            m.tint = make_float3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
            m.roughness = pbr.roughness_factor;
            const std::string alb = imagePath(pbr.base_color_texture);
            if (!alb.empty()) {
                const int t = texture(alb, Albedo);
                if (t >= 0) {
                    m.albedo = textures[t].tex;
                    m.texelArea = float(textures[t].w) * float(textures[t].h);
                }
            }
            const std::string nor = imagePath(gm->normal_texture);
            if (!nor.empty()) {
                const int t = texture(nor, Normal);
                if (t >= 0) m.normal = textures[t].tex;
            }
            const std::string arm = imagePath(pbr.metallic_roughness_texture);
            if (!arm.empty()) {
                const int t = texture(arm, Linear);
                if (t >= 0) m.arm = textures[t].tex;
            }
            if (gm->double_sided) m.flags |= OMAT_DOUBLE;
            // (a blended leaf is cut too: the forest has no glass, and a cut edge stays sharp at any distance)
            if (gm->alpha_mode == cgltf_alpha_mode_mask || gm->alpha_mode == cgltf_alpha_mode_blend) {
                m.alphaCutoff = gm->alpha_mode == cgltf_alpha_mode_mask ? gm->alpha_cutoff : 0.5f;
                // Poly Haven ships the base colour as a JPG and the alpha beside it (textures/<id>_alpha_2k.png); a PNG
                // base colour carries its own
                // (its image is named "<diffuse>-<alpha>": the alpha's own stem first, any alpha in the folder after)
                std::string a;
                const cgltf_image* im = pbr.base_color_texture.texture ? pbr.base_color_texture.texture->image : nullptr;
                if (im && im->name) {
                    const std::string nm = im->name;
                    const size_t dash = nm.rfind('-');
                    if (dash != std::string::npos && nm.find("alpha", dash) != std::string::npos) a = findFile(base, {nm.substr(dash + 1)});
                }
                if (a.empty()) a = findFile(base, {"_alpha"});
                bool fromAlbedo = false;
                if (a.empty() && fs::path(alb).extension() == ".png") {
                    a = alb;
                    fromAlbedo = true;
                }
                if (!a.empty()) {
                    const int t = texture(a, Alpha, m.alphaCutoff, fromAlbedo);
                    if (t >= 0) {
                        m.alpha = textures[t].tex;
                        m.flags |= OMAT_ALPHA;
                    }
                }
                m.flags |= OMAT_FOLIAGE | OMAT_DOUBLE;
                m.translucency = 0.35f;
            }
        }
        if (!m.albedo) {
            std::fprintf(stderr, "orman: %s renksiz malzeme\n", name.c_str());
        }
        const int k = addMaterial(m, name);
        matIndex[gm] = k;
        return k;
    };

    int made = 0;
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        // only the finest of a model's levels of detail (the ray tracer needs no coarser copy)
        if (node->name) {
            const std::string nm = node->name;
            const size_t lod = nm.find("_LOD");
            if (lod != std::string::npos && nm.compare(lod, 5, "_LOD0") != 0) continue;
        }
        float m[16];
        cgltf_node_transform_world(node, m);
        // the variants stand side by side in the file: each one at its own origin (the root's translation dropped)
        const cgltf_node* top = node;
        while (top->parent) top = top->parent;
        float tm[16];
        cgltf_node_transform_world(top, tm);
        m[12] -= tm[12];
        m[13] -= tm[13];
        m[14] -= tm[14];
        OProtoCPU p;
        p.asset = id;
        p.name = node->name ? node->name : id;
        p.solid = solid;
        p.bmin = make_float3(1e30f, 1e30f, 1e30f);
        p.bmax = make_float3(-1e30f, -1e30f, -1e30f);
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& pr = node->mesh->primitives[pi];
            if (pr.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *ap = nullptr, *an = nullptr, *at = nullptr;
            for (cgltf_size a = 0; a < pr.attributes_count; ++a) {
                const cgltf_attribute& at0 = pr.attributes[a];
                if (at0.type == cgltf_attribute_type_position) ap = at0.data;
                else if (at0.type == cgltf_attribute_type_normal) an = at0.data;
                else if (at0.type == cgltf_attribute_type_texcoord && at0.index == 0) at = at0.data;
            }
            if (!ap || !at) continue;
            OMeshCPU g;
            const size_t nv = ap->count;
            g.pos.resize(nv);
            g.nrm.resize(an ? nv : 0);
            g.uv.resize(nv);
            for (size_t v = 0; v < nv; ++v) {
                float q[3] = {0, 0, 0};
                cgltf_accessor_read_float(ap, v, q, 3);
                const float3 w = make_float3(m[0] * q[0] + m[4] * q[1] + m[8] * q[2] + m[12], m[1] * q[0] + m[5] * q[1] + m[9] * q[2] + m[13],
                                             m[2] * q[0] + m[6] * q[1] + m[10] * q[2] + m[14]);
                g.pos[v] = w;
                p.bmin = make_float3(std::min(p.bmin.x, w.x), std::min(p.bmin.y, w.y), std::min(p.bmin.z, w.z));
                p.bmax = make_float3(std::max(p.bmax.x, w.x), std::max(p.bmax.y, w.y), std::max(p.bmax.z, w.z));
                if (an) {
                    float r[3] = {0, 0, 1};
                    cgltf_accessor_read_float(an, v, r, 3);
                    float3 nn = make_float3(m[0] * r[0] + m[4] * r[1] + m[8] * r[2], m[1] * r[0] + m[5] * r[1] + m[9] * r[2], m[2] * r[0] + m[6] * r[1] + m[10] * r[2]);
                    const float l = std::sqrt(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
                    g.nrm[v] = l > 0 ? nn * (1.0f / l) : make_float3(0, 1, 0);
                }
                float t[2] = {0, 0};
                cgltf_accessor_read_float(at, v, t, 2);
                g.uv[v] = make_float2(t[0], t[1]);
            }
            if (pr.indices) {
                g.idx.resize(pr.indices->count / 3);
                for (size_t k = 0; k < g.idx.size(); ++k)
                    g.idx[k] = make_uint3(unsigned(cgltf_accessor_read_index(pr.indices, k * 3)), unsigned(cgltf_accessor_read_index(pr.indices, k * 3 + 1)),
                                          unsigned(cgltf_accessor_read_index(pr.indices, k * 3 + 2)));
            } else {
                g.idx.resize(nv / 3);
                for (size_t k = 0; k < g.idx.size(); ++k) g.idx[k] = make_uint3(unsigned(3 * k), unsigned(3 * k + 1), unsigned(3 * k + 2));
            }
            g.material = material(pr.material);
            p.geoms.push_back(std::move(g));
            if (p.geoms.size() >= ORMAN_MAX_INPUTS) break;
        }
        if (p.geoms.empty()) continue;
        byAsset[id].push_back(int(protos.size()));
        protos.push_back(std::move(p));
        ++made;
    }
    cgltf_free(data);
    if (!made) missing.push_back(id);
    return made > 0;
}

int OrmanAssets::loadMaterial(const std::string& id) {
    const std::string dir = root + "/" + id;
    const std::string alb = findFile(dir, {"_diff", "_color"});
    if (alb.empty()) {
        missing.push_back(id);
        return -1;
    }
    OMaterial m{};
    m.tint = make_float3(1, 1, 1);
    m.roughness = 0.85f;
    m.alphaCutoff = 0.5f;
    const int ta = texture(alb, Albedo);
    if (ta < 0) return -1;
    m.albedo = textures[ta].tex;
    m.texelArea = float(textures[ta].w) * float(textures[ta].h);
    const std::string nor = findFile(dir, {"_nor_gl", "_normalgl"});
    if (!nor.empty()) {
        const int t = texture(nor, Normal);
        if (t >= 0) m.normal = textures[t].tex;
    }
    const std::string arm = findFile(dir, {"_arm"});
    if (!arm.empty()) {
        const int t = texture(arm, Linear);
        if (t >= 0) m.arm = textures[t].tex;
    } else {
        // occlusion and roughness into one ORM texture (R occlusion, G roughness)
        const std::string ro = findFile(dir, {"_rough"}), ao = findFile(dir, {"_ao", "_ambientocclusion"});
        if (!ro.empty()) {
            int w = 0, h = 0, n = 0, w2 = 0, h2 = 0;
            unsigned char* r = stbi_load(ro.c_str(), &w, &h, &n, 1);
            unsigned char* a = ao.empty() ? nullptr : stbi_load(ao.c_str(), &w2, &h2, &n, 1);
            if (r) {
                const std::string key = ro + "|orm";
                auto it = texIndex_.find(key);
                int t = it == texIndex_.end() ? -1 : it->second;
                if (it == texIndex_.end()) {
                    std::vector<Level> levels(1);
                    levels[0].w = w;
                    levels[0].h = h;
                    levels[0].px.resize(size_t(w) * h * 4);
                    for (size_t i = 0; i < size_t(w) * h; ++i) {
                        levels[0].px[i * 4 + 0] = (a && w2 == w && h2 == h) ? a[i] : 255;
                        levels[0].px[i * 4 + 1] = r[i];
                        levels[0].px[i * 4 + 2] = 0;
                        levels[0].px[i * 4 + 3] = 255;
                    }
                    chain(levels, 4);
                    OTexture tx;
                    if (uploadLevels(levels, 4, false, tx)) {
                        textures.push_back(tx);
                        t = int(textures.size()) - 1;
                    }
                    texIndex_[key] = t;
                }
                if (t >= 0) m.arm = textures[t].tex;
                stbi_image_free(r);
            }
            if (a) stbi_image_free(a);
        }
    }
    return addMaterial(m, id);
}

size_t OrmanAssets::textureBytes() const {
    size_t n = 0;
    for (const auto& t : textures) n += t.bytes;
    return n;
}

void OrmanAssets::release() {
    for (auto& t : textures) {
        if (t.tex) cudaDestroyTextureObject(t.tex);
        if (t.arr) cudaFreeMipmappedArray(t.arr);
    }
    textures.clear();
    texIndex_.clear();
    protos.clear();
    materials.clear();
    materialNames.clear();
    byAsset.clear();
    missing.clear();
}
