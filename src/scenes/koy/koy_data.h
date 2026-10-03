// Scene 5 "Koy": published data the optics are built from (koy_optics.cpp interpolates them onto its 5 nm grid).
// Every table names its source; docs/koy/VERI.md lists the sources and how each value was checked.
//
// GEÇİCİ (2026-10-03): these first values are placeholders for the first build; the research pass (K0) replaces each
// table with the published one and marks it checked.
#pragma once

struct KoyPoint {
    float nm, v;
};

// Pure water absorption, 1/m. GEÇİCİ: rounded values at 10 nm until Pope & Fry 1997 (Applied Optics 36(33)) is pinned.
static const KoyPoint kWaterAbsorption[] = {
    {380, 0.0114f}, {390, 0.0085f}, {400, 0.0066f}, {410, 0.0047f}, {420, 0.0045f}, {430, 0.0050f}, {440, 0.0064f}, {450, 0.0092f},
    {460, 0.0098f}, {470, 0.0106f}, {480, 0.0127f}, {490, 0.0150f}, {500, 0.0204f}, {510, 0.0325f}, {520, 0.0409f}, {530, 0.0434f},
    {540, 0.0474f}, {550, 0.0565f}, {560, 0.0619f}, {570, 0.0695f}, {580, 0.0896f}, {590, 0.1351f}, {600, 0.2224f}, {610, 0.2644f},
    {620, 0.2755f}, {630, 0.2916f}, {640, 0.3108f}, {650, 0.3400f}, {660, 0.4100f}, {670, 0.4390f}, {680, 0.4650f}, {690, 0.5160f},
    {700, 0.6240f}, {710, 0.8270f}, {720, 1.2310f}, {730, 1.9f}, {750, 2.6f}, {780, 2.3f}};

// Direct normal and global (37 degree tilt) solar irradiance at the ground, W m^-2 nm^-1. GEÇİCİ: coarse values at 20 nm
// until ASTM G173-03 (NREL) is pinned.
static const KoyPoint kSunDirect[] = {
    {380, 0.68f}, {400, 0.87f}, {420, 1.04f}, {440, 1.12f}, {460, 1.28f}, {480, 1.33f}, {500, 1.30f}, {520, 1.27f}, {540, 1.30f},
    {560, 1.31f}, {580, 1.31f}, {600, 1.31f}, {620, 1.30f}, {640, 1.27f}, {660, 1.27f}, {680, 1.25f}, {700, 1.19f}, {720, 1.03f},
    {740, 1.13f}, {760, 0.40f}, {780, 1.05f}};
static const KoyPoint kSunGlobal[] = {
    {380, 0.92f}, {400, 1.12f}, {420, 1.30f}, {440, 1.37f}, {460, 1.55f}, {480, 1.58f}, {500, 1.53f}, {520, 1.47f}, {540, 1.50f},
    {560, 1.49f}, {580, 1.48f}, {600, 1.47f}, {620, 1.44f}, {640, 1.40f}, {660, 1.39f}, {680, 1.36f}, {700, 1.30f}, {720, 1.12f},
    {740, 1.22f}, {760, 0.44f}, {780, 1.13f}};
