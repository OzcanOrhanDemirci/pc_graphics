// Plant and scatter prototypes. Foliage is built from broad crown forms: opaque
// lobes carry the volume and coverage, and leaf pads about half a metre wide
// break the silhouette. Nothing here is thin enough to fall below a pixel at
// the distances fog allows, which is what kept fixed-sample foliage stable.
// Every plant has a dark root that starts underground; placement embeds it.

void fractureShard(Mesh& mesh, float3 center, float3 size, unsigned seed);

constexpr float kRootDepth = 1.2f;

float3 bezier(float3 a, float3 b, float3 c, float t) {
    const float u = 1.0f - t;
    return a * (u * u) + b * (2.0f * u * t) + c * (t * t);
}

// A tapered curve, used for trunks and limbs.
void limb(Mesh& mesh, float3 a, float3 mid, float3 b, float radiusA, float radiusB, int sides, int segments) {
    const auto center = [&](float t) { return bezier(a, mid, b, clampf(t, 0.0f, 1.0f)); };
    const auto radial = [&](float u, float v) {
        const float3 axis = safeNormal(center(v + 0.01f) - center(v - 0.01f), make_float3(0, 1, 0));
        float3 tangent, bitangent;
        makeBasis(axis, tangent, bitangent);
        return tangent * std::cos(u * kTau) + bitangent * std::sin(u * kTau);
    };
    surface(mesh, sides, segments, kMaterialDarkStone,
            [&](float u, float v) { return center(v) + radial(u, v) * lerpf(radiusA, radiusB, clampf(v, 0.0f, 1.0f)); },
            radial);
}

// One opaque lobe of a crown, displaced so it does not read as a sphere.
void lobe(Mesh& mesh, float3 center, float3 radius, unsigned seed, int detail) {
    const int columns = 6 + 2 * detail;
    const int rows = 3 + detail;
    const auto direction = [](float u, float v) {
        const float latitude = v * M_PIf;
        return make_float3(std::sin(latitude) * std::cos(u * kTau), std::cos(latitude), std::sin(latitude) * std::sin(u * kTau));
    };
    surface(mesh, columns, rows, kMaterialLeaf,
            [&](float u, float v) {
                const float3 d = direction(u, v);
                const float displacement = 1.0f + 0.24f * noise(d * 2.4f, seed) + 0.10f * noise(d * 5.2f, seed + 9U);
                return center + d * radius * displacement;
            },
            [&](float u, float v) {
                const float3 d = direction(u, v);
                return make_float3(d.x / radius.x, d.y / radius.y, d.z / radius.z);
            });
}

// A flat, slightly cupped pad of leaves, facing roughly along outward.
void leafPad(Mesh& mesh, float3 center, float3 outward, float radius, unsigned seed) {
    const float3 up = safeNormal(outward * 0.92f + make_float3(0, 0.3f, 0), make_float3(0, 1, 0));
    float3 tangent, bitangent;
    makeBasis(up, tangent, bitangent);
    constexpr int kSides = 6;
    const unsigned first = unsigned(mesh.vertices.size());
    mesh.vertices.push_back({center + up * (radius * 0.16f), up, kMaterialLeaf});
    for (int side = 0; side < kSides; ++side) {
        const unsigned key = seed + unsigned(side) * 53U;
        const float angle = kTau * (float(side) + 0.5f * randomUnit(key)) / float(kSides);
        const float3 spoke = tangent * std::cos(angle) + bitangent * std::sin(angle);
        const float reach = radius * (0.75f + 0.35f * randomUnit(key + 1U));
        const float3 point = center + spoke * reach - up * (radius * 0.2f * randomUnit(key + 2U));
        mesh.vertices.push_back({point, safeNormal(up * 0.75f + spoke * 0.4f, up), kMaterialLeaf});
    }
    for (unsigned side = 0; side < kSides; ++side)
        triangle(mesh, first, first + 1U + side, first + 1U + (side + 1U) % kSides);
}

// A crown: lobes inside for coverage, pads on the shell for silhouette.
void canopy(Mesh& mesh, float3 center, float3 radius, unsigned seed, int detail) {
    const float size = std::max({radius.x, radius.y, radius.z});
    const int lobes = std::clamp(int(size * 1.8f) + 2, 2, 8);
    for (int index = 0; index < lobes; ++index) {
        const unsigned key = seed + unsigned(index) * 491U;
        const float3 offset = make_float3(randomUnit(key) - 0.5f, randomUnit(key + 1U) - 0.5f, randomUnit(key + 2U) - 0.5f) * radius * 1.05f;
        const float scale = 0.5f + 0.2f * randomUnit(key + 3U);
        lobe(mesh, center + offset, radius * scale, key + 5U, detail);
    }
    const int pads = int(std::min(size * size * 26.0f + 34.0f, 260.0f));
    const float padRadius = std::max(0.17f, 0.13f + 0.05f * size);
    for (int index = 0; index < pads; ++index) {
        const unsigned key = seed + 7919U + unsigned(index) * 263U;
        const float theta = kTau * randomUnit(key);
        const float vertical = 2.0f * randomUnit(key + 1U) - 1.0f;
        const float horizontal = std::sqrt(std::max(0.0f, 1.0f - vertical * vertical));
        const float3 direction = make_float3(horizontal * std::cos(theta), vertical, horizontal * std::sin(theta));
        const float shell = 0.82f + 0.3f * randomUnit(key + 2U);
        leafPad(mesh, center + direction * radius * shell, direction, padRadius * (0.8f + 0.5f * randomUnit(key + 3U)), key + 11U);
    }
}

void olive(Mesh& mesh, float3 base, float height, unsigned seed, int detail) {
    const float3 root = base - make_float3(0, kRootDepth, 0);
    const float3 lean = make_float3(0.6f * (randomUnit(seed) - 0.5f), 0, 0.6f * (randomUnit(seed + 1U) - 0.5f));
    const float3 fork = base + make_float3(lean.x, height * 0.42f, lean.z);
    limb(mesh, root, base + make_float3(0, height * 0.18f, 0), fork, 0.23f, 0.15f, 6 + detail, 3);
    const int branches = 3 + int(seed % 3U);
    for (int branch = 0; branch < branches; ++branch) {
        const unsigned key = seed + unsigned(branch) * 277U;
        const float angle = kTau * (float(branch) + 0.5f * randomUnit(key)) / float(branches);
        const float reach = 0.32f * height * (0.8f + 0.5f * randomUnit(key + 1U));
        const float3 end = fork + make_float3(std::cos(angle) * reach, height * (0.24f + 0.24f * randomUnit(key + 2U)), std::sin(angle) * reach);
        limb(mesh, fork, fork + (end - fork) * 0.5f + make_float3(0, -0.12f * height, 0), end, 0.13f, 0.06f, 5, 3);
        const float size = 0.34f * height * (0.8f + 0.4f * randomUnit(key + 3U));
        canopy(mesh, end + make_float3(0, size * 0.25f, 0), make_float3(size, size * 0.68f, size * 0.92f), key + 13U, detail);
    }
    canopy(mesh, fork + make_float3(0, height * 0.42f, 0), make_float3(height * 0.34f, height * 0.26f, height * 0.32f), seed + 31U, detail);
}

void cypress(Mesh& mesh, float3 base, float height, unsigned seed, int detail) {
    limb(mesh, base - make_float3(0, kRootDepth, 0), base + make_float3(0.08f, height * 0.4f, 0),
         base + make_float3(0.12f, height * 0.92f, 0), 0.17f, 0.05f, 6, 3);
    constexpr int kTiers = 5;
    for (int tier = 0; tier < kTiers; ++tier) {
        const float v = float(tier) / float(kTiers - 1);
        const float radius = height * 0.135f * (1.0f - 0.72f * v) * (0.9f + 0.25f * std::sin(M_PIf * v));
        const float3 center = base + make_float3(0.12f * v, height * (0.20f + 0.70f * v), 0);
        canopy(mesh, center, make_float3(radius, height * 0.15f, radius), seed + unsigned(tier) * 1103U, detail);
    }
}

// Huangshan pine: a crooked trunk and flat tiers of foliage on long limbs.
void pineFlat(Mesh& mesh, float3 base, float height, unsigned seed, int detail) {
    const float3 lean = make_float3(1.1f * (randomUnit(seed + 2U) - 0.5f), 0, 1.1f * (randomUnit(seed + 3U) - 0.5f));
    const float3 top = base + make_float3(lean.x, height, lean.z);
    const float3 mid = base + make_float3(-lean.x * 0.8f, height * 0.5f, lean.z * 0.4f);
    limb(mesh, base - make_float3(0, kRootDepth, 0), mid, top, 0.32f, 0.09f, 7, 5);
    const int tiers = 5 + int(seed % 3U);
    float heading = kTau * randomUnit(seed + 5U);
    for (int tier = 0; tier < tiers; ++tier) {
        const unsigned key = seed + unsigned(tier) * 1553U;
        const float t = 0.42f + 0.58f * float(tier) / float(tiers - 1);
        const float3 start = bezier(base - make_float3(0, kRootDepth, 0), mid, top, t);
        heading += 2.1f + randomUnit(key) * 0.9f;
        const float reach = lerpf(0.34f, 0.16f, t) * height * (0.85f + 0.3f * randomUnit(key + 1U));
        const float3 end = start + make_float3(std::cos(heading) * reach, height * 0.03f, std::sin(heading) * reach);
        limb(mesh, start, start + (end - start) * 0.55f + make_float3(0, height * 0.015f, 0), end, 0.08f, 0.035f, 5, 3);
        const float wide = lerpf(0.34f, 0.2f, t) * height;
        for (int clump = 0; clump < 3; ++clump) {
            const float bias = clump == 0 ? 0.0f : clump == 1 ? -0.42f : 0.42f;
            const float3 at = end + make_float3(std::cos(heading + 1.57f * bias) * wide * bias, 0.04f * height * float(clump % 2),
                                                 std::sin(heading + 1.57f * bias) * wide * bias * 0.8f);
            canopy(mesh, at, make_float3(wide * 0.66f, height * 0.058f, wide * 0.56f), key + 7U + unsigned(clump) * 13U, detail);
        }
    }
    canopy(mesh, top + make_float3(0, height * 0.03f, 0), make_float3(height * 0.15f, height * 0.08f, height * 0.13f), seed + 77U, detail);
}

// A cliff pine: rooted in the rock at the origin, growing out along +x.
void pineCling(Mesh& mesh, float reach, float height, unsigned seed, int detail) {
    const float3 root = make_float3(-1.0f, -0.6f, 0);
    const float3 knee = make_float3(reach * 0.55f, height * 0.12f, 0.35f * (randomUnit(seed) - 0.5f));
    const float3 top = make_float3(reach, height, 0.8f * (randomUnit(seed + 1U) - 0.5f));
    limb(mesh, root, knee, top, 0.24f, 0.08f, 7, 5);
    const int tiers = 4 + int(seed % 2U);
    for (int tier = 0; tier < tiers; ++tier) {
        const unsigned key = seed + unsigned(tier) * 691U;
        const float t = 0.5f + 0.5f * float(tier) / float(tiers - 1);
        const float3 start = bezier(root, knee, top, t);
        const float angle = 0.6f * (randomUnit(key) - 0.5f) + (tier % 2 ? 1.0f : -1.0f) * (0.5f + 0.7f * randomUnit(key + 1U));
        const float length = lerpf(0.36f, 0.2f, t) * height * (0.75f + 0.5f * randomUnit(key + 2U));
        const float3 end = start + make_float3(std::cos(angle) * length, height * 0.05f, std::sin(angle) * length);
        limb(mesh, start, start + (end - start) * 0.6f, end, 0.06f, 0.028f, 5, 2);
        const float wide = lerpf(0.34f, 0.18f, t) * height;
        for (int clump = 0; clump < 2; ++clump)
            canopy(mesh, end + make_float3(0, height * 0.05f * float(clump), wide * 0.25f * (clump ? 1.0f : -1.0f)),
                   make_float3(wide * 0.7f, height * 0.08f, wide * 0.6f), key + 3U + unsigned(clump) * 17U, detail);
    }
}

void shrub3(Mesh& mesh, float3 base, float size, unsigned seed, int detail) {
    const float3 crown = base + make_float3(0, size * 0.55f, 0);
    limb(mesh, base - make_float3(0, kRootDepth * 0.6f, 0), base + make_float3(0, size * 0.2f, 0), crown, 0.09f, 0.04f, 5, 2);
    canopy(mesh, crown, make_float3(size, size * 0.72f, size * 0.92f), seed, detail);
}

// Low ground cover: a flat mat of foliage that hides the ground under trees.
void groundMat(Mesh& mesh, float3 base, float size, unsigned seed, int detail) {
    limb(mesh, base - make_float3(0, kRootDepth * 0.5f, 0), base + make_float3(0, size * 0.1f, 0), base + make_float3(0, size * 0.2f, 0), 0.08f, 0.04f, 5, 2);
    canopy(mesh, base + make_float3(0, size * 0.3f, 0), make_float3(size * 0.78f, size * 0.46f, size * 0.7f), seed, detail);
}

void grove(Mesh& mesh, int detail, unsigned seed, int broad, int pines, int tall, float span, int floors) {
    const auto spot = [&](unsigned key) {
        const float angle = kTau * randomUnit(key);
        const float along = span * std::sqrt(randomUnit(key + 1U));
        return make_float3(std::cos(angle) * along, 0, std::sin(angle) * along);
    };
    for (int tree = 0; tree < broad; ++tree) {
        const unsigned key = seed + unsigned(tree) * 1447U;
        olive(mesh, spot(key), 4.4f + 2.2f * randomUnit(key + 2U), key + 7U, detail);
    }
    for (int tree = 0; tree < pines; ++tree) {
        const unsigned key = seed + 501U + unsigned(tree) * 2111U;
        pineFlat(mesh, spot(key), 8.5f + 3.5f * randomUnit(key + 2U), key + 7U, detail);
    }
    for (int tree = 0; tree < tall; ++tree) {
        const unsigned key = seed + 907U + unsigned(tree) * 3121U;
        cypress(mesh, spot(key), 8.2f + 3.2f * randomUnit(key + 2U), key + 7U, detail);
    }
    for (int mat = 0; mat < floors; ++mat) {
        const unsigned key = seed + 1307U + unsigned(mat) * 883U;
        groundMat(mesh, spot(key) * 1.12f, 1.6f + 1.2f * randomUnit(key + 2U), key + 7U, detail);
    }
}

void buildVegetation(std::vector<Mesh>& result, int detail) {
    pineFlat(result[kProtoPineFlat], make_float3(0, 0, 0), 11.0f, 3137, detail);
    pineCling(result[kProtoPineLean], 3.6f, 7.5f, 4441, detail);
    pineCling(result[kProtoPineCling], 2.2f, 5.0f, 6113, detail);
    olive(result[kProtoOlive], make_float3(0, 0, 0), 5.2f, 51577, detail);
    cypress(result[kProtoCypress], make_float3(0, 0, 0), 8.3f, 8761, detail);
    grove(result[kProtoGroveBroad], detail, 2711, 8, 1, 2, 8.0f, 9);
    grove(result[kProtoGroveMixed], detail, 3923, 3, 4, 3, 8.5f, 8);
    grove(result[kProtoGroveDense], detail, 7529, 12, 2, 1, 9.5f, 14);
    grove(result[kProtoThicket], detail, 6607, 3, 0, 1, 3.6f, 5);
    for (int shrub = 0; shrub < 8; ++shrub) {
        const unsigned key = 8831U + unsigned(shrub) * 757U;
        const float angle = kTau * float(shrub) / 8.0f + randomUnit(key);
        const float along = 3.2f * std::sqrt(randomUnit(key + 1U));
        shrub3(result[kProtoShrubs], make_float3(std::cos(angle) * along, 0, std::sin(angle) * along),
               0.7f + 0.8f * randomUnit(key + 2U), key + 3U, detail);
    }
    for (int shrub = 0; shrub < 9; ++shrub) {
        const unsigned key = 1361U + unsigned(shrub) * 907U;
        const float z = -4.2f + 8.4f * float(shrub) / 8.0f;
        shrub3(result[kProtoHedge], make_float3((randomUnit(key) - 0.5f) * 0.8f, 0, z),
               0.55f + 0.5f * randomUnit(key + 1U), key + 2U, detail);
    }
    for (int rock = 0; rock < 11; ++rock) {
        const unsigned key = 2239U + unsigned(rock) * 449U;
        const float angle = kTau * randomUnit(key);
        const float along = 3.0f * std::sqrt(randomUnit(key + 1U));
        const float size = 0.5f + 1.1f * randomUnit(key + 2U);
        stone(result[kProtoBoulders], make_float3(std::cos(angle) * along, size * 0.3f, std::sin(angle) * along),
              make_float3(size, size * (0.55f + 0.5f * randomUnit(key + 3U)), size * 0.82f), key, 1,
              rock % 4 == 0 ? kMaterialDarkStone : kMaterialStone);
    }
    for (int shard = 0; shard < 26; ++shard) {
        const unsigned key = 3041U + unsigned(shard) * 331U;
        const float angle = kTau * randomUnit(key);
        const float along = 3.4f * std::sqrt(randomUnit(key + 1U));
        const float size = 0.18f + 0.5f * randomUnit(key + 2U);
        fractureShard(result[kProtoRubble], make_float3(std::cos(angle) * along, size * 0.7f, std::sin(angle) * along),
                      make_float3(size, size * 0.75f, size * 0.85f), key);
    }
    // A ledge plant: a low crown over the root, trailing masses out over the drop.
    {
        Mesh& tuft = result[kProtoLedgeTuft];
        limb(tuft, make_float3(0, -0.5f, 0), make_float3(0.05f, -0.1f, 0), make_float3(0.1f, 0.25f, 0), 0.09f, 0.05f, 5, 2);
        canopy(tuft, make_float3(0.05f, 0.42f, 0), make_float3(0.75f, 0.42f, 0.7f), 1187, detail);
        for (int trail = 0; trail < 4; ++trail) {
            const float z = -0.55f + 0.37f * float(trail);
            const float length = 0.7f + 0.3f * float((trail * 7) % 3);
            canopy(tuft, make_float3(0.55f + 0.08f * float(trail % 2), 0.1f - length, z),
                   make_float3(0.26f, length, 0.3f), 1291U + unsigned(trail) * 97U, 1);
        }
    }
    groundMat(result[kProtoGroundCover], make_float3(0, 0, 0), 2.6f, 2069, detail);
    groundMat(result[kProtoGroundCover], make_float3(2.4f, 0, 1.2f), 1.8f, 2083, detail);
    groundMat(result[kProtoGroundCover], make_float3(-2.1f, 0, -1.4f), 2.1f, 2099, detail);
    // A standing tumble of cut blocks.
    Mesh& stack = result[kProtoRockStack];
    fractureShard(stack, make_float3(0, 1.1f, 0), make_float3(1.6f, 2.6f, 1.4f), 3301);
    fractureShard(stack, make_float3(0.4f, 3.6f, -0.2f), make_float3(1.05f, 1.5f, 0.9f), 3313);
    fractureShard(stack, make_float3(-1.6f, 0.7f, 0.9f), make_float3(1.0f, 1.0f, 0.9f), 3323);
    fractureShard(stack, make_float3(1.5f, 0.6f, -1.2f), make_float3(0.8f, 0.8f, 0.8f), 3329);
}

void recenter(Mesh& mesh) {
    float3 low = mesh.vertices.front().position;
    float3 high = low;
    for (const Vertex& vertex : mesh.vertices) {
        low = make_float3(std::min(low.x, vertex.position.x), 0, std::min(low.z, vertex.position.z));
        high = make_float3(std::max(high.x, vertex.position.x), 0, std::max(high.z, vertex.position.z));
    }
    const float3 center = (low + high) * 0.5f;
    for (Vertex& vertex : mesh.vertices) {
        vertex.position.x -= center.x;
        vertex.position.z -= center.z;
    }
}
