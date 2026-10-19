// Tone curve constants shared by C++ (Core/ToneCurves.h) and GLSL (the stage's
// shader loader expands every HALO_TC (name, value) into "const float name = value;").
// One set of numbers, two languages: edit here only.
//
// No #pragma once and no other syntax: this file is included more than once.

// Khronos PBR Neutral (Khronos Group, 2024).
HALO_TC (neutralStartCompression, 0.76)
HALO_TC (neutralDesaturation, 0.15)
HALO_TC (neutralToeKnee, 0.08)
HALO_TC (neutralToeScale, 6.25)
HALO_TC (neutralOffset, 0.04)

// AgX, Troy Sobotka's base (minimal fit by Benjamin Wrensch, 2023).
HALO_TC (agxMinEv, -12.47393)
HALO_TC (agxMaxEv, 4.026069)
// Inset matrix, column-major (GLSL mat3 order).
HALO_TC (agxIn00, 0.842479062253094)
HALO_TC (agxIn01, 0.0423282422610123)
HALO_TC (agxIn02, 0.0423756549057051)
HALO_TC (agxIn10, 0.0784335999999992)
HALO_TC (agxIn11, 0.878468636469772)
HALO_TC (agxIn12, 0.0784336)
HALO_TC (agxIn20, 0.0792237451477643)
HALO_TC (agxIn21, 0.0791661274605434)
HALO_TC (agxIn22, 0.879142973793104)
// Outset matrix, column-major.
HALO_TC (agxOut00, 1.19687900512017)
HALO_TC (agxOut01, -0.0528968517574562)
HALO_TC (agxOut02, -0.0529716355144438)
HALO_TC (agxOut10, -0.0980208811401368)
HALO_TC (agxOut11, 1.15190312990417)
HALO_TC (agxOut12, -0.0980434501171241)
HALO_TC (agxOut20, -0.0990297440797205)
HALO_TC (agxOut21, -0.0989611768448433)
HALO_TC (agxOut22, 1.15107367264116)
// Sigmoid polynomial: c6 x^6 + c5 x^5 + c4 x^4 + c3 x^3 + c2 x^2 + c1 x + c0.
HALO_TC (agxC6, 15.5)
HALO_TC (agxC5, -40.14)
HALO_TC (agxC4, 31.96)
HALO_TC (agxC3, -6.868)
HALO_TC (agxC2, 0.4298)
HALO_TC (agxC1, 0.1191)
HALO_TC (agxC0, -0.00232)
HALO_TC (agxDisplayGamma, 2.2)

// ACES, Stephen Hill's fit of the RRT + sRGB ODT (comparison only).
// Input and output matrices, row-major (row r, column c).
HALO_TC (acesIn00, 0.59719)
HALO_TC (acesIn01, 0.35458)
HALO_TC (acesIn02, 0.04823)
HALO_TC (acesIn10, 0.07600)
HALO_TC (acesIn11, 0.90834)
HALO_TC (acesIn12, 0.01566)
HALO_TC (acesIn20, 0.02840)
HALO_TC (acesIn21, 0.13383)
HALO_TC (acesIn22, 0.83777)
HALO_TC (acesOut00, 1.60475)
HALO_TC (acesOut01, -0.53108)
HALO_TC (acesOut02, -0.07367)
HALO_TC (acesOut10, -0.10208)
HALO_TC (acesOut11, 1.10813)
HALO_TC (acesOut12, -0.00605)
HALO_TC (acesOut20, -0.00327)
HALO_TC (acesOut21, -0.07276)
HALO_TC (acesOut22, 1.07602)
HALO_TC (acesA, 0.0245786)
HALO_TC (acesB, 0.000090537)
HALO_TC (acesC, 0.983729)
HALO_TC (acesD, 0.4329510)
HALO_TC (acesE, 0.238081)

// sRGB (IEC 61966-2-1) piecewise transfer function.
HALO_TC (srgbLinearKnee, 0.0031308)
HALO_TC (srgbEncodedKnee, 0.04045)
HALO_TC (srgbLinearSlope, 12.92)
HALO_TC (srgbScale, 1.055)
HALO_TC (srgbOffset, 0.055)
HALO_TC (srgbGamma, 2.4)
