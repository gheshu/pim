#pragma once

#include "common/macro.h"

PIM_C_BEGIN

#include "math/types.h"
#include "math/int2_funcs.h"
#include "math/float2_funcs.h"
#include "math/float4_funcs.h"
#include "math/color.h"
#include "rendering/texture.h"

pim_inline float2 VEC_CALL TransformUv(float2 uv, float4 st)
{
    uv.x = uv.x * st.x + st.z;
    uv.y = uv.y * st.y + st.w;
    return uv;
}

pim_inline float2 VEC_CALL Tex_UvToCoordf(int2 size, float2 uv)
{
    return f2_add(f2_mul(uv, i2_f2(size)), f2_s(-0.5f));
}

pim_inline int2 VEC_CALL Tex_UvToCoord(int2 size, float2 uv)
{
    return f2_i2(Tex_UvToCoordf(size, uv));
}

pim_inline int2 VEC_CALL Tex_ClampCoord(int2 size, int2 coord)
{
    return i2_clamp(coord, i2_s(0), i2_add(size, i2_s(-1)));
}

pim_inline int2 VEC_CALL Tex_WrapCoord(int2 size, int2 coord)
{
    coord = i2_abs(coord);
    int2 y = { coord.x & size.x, coord.y % size.y };
    return y;
}

pim_inline i32 VEC_CALL Tex_CoordToIndex(int2 size, int2 coord)
{
    return coord.x + coord.y * size.x;
}

pim_inline float4 VEC_CALL Tex_Nearesti2(texture_t texture, int2 coord)
{
    coord = Tex_WrapCoord(texture.size, coord);
    i32 i = Tex_CoordToIndex(texture.size, coord);
    return ColorToLinear(texture.texels[i]);
}

pim_inline float4 VEC_CALL Tex_Nearestf2(texture_t texture, float2 uv)
{
    return Tex_Nearesti2(texture, Tex_UvToCoord(texture.size, uv));
}

pim_inline float4 VEC_CALL Tex_Bilinearf2(texture_t texture, float2 uv)
{
    float2 coordf = Tex_UvToCoordf(texture.size, uv);
    float2 frac = f2_frac(coordf);
    int2 ia = f2_i2(coordf);
    int2 ib = { ia.x + 1, ia.y + 0 };
    int2 ic = { ia.x + 0, ia.y + 1 };
    int2 id = { ia.x + 1, ia.y + 1 };
    float4 a = Tex_Nearesti2(texture, ia);
    float4 b = Tex_Nearesti2(texture, ib);
    float4 c = Tex_Nearesti2(texture, ic);
    float4 d = Tex_Nearesti2(texture, id);
    float4 e = f4_lerpvs(f4_lerpvs(a, b, frac.x), f4_lerpvs(c, d, frac.x), frac.y);
    return e;
}

pim_inline void VEC_CALL Tex_Writei1(
    texture_t texture, i32 i, float4 value)
{
    texture.texels[i] = LinearToColor(value);
}

pim_inline void VEC_CALL Tex_Writei2(
    texture_t texture, int2 coord, float4 value)
{
    coord = Tex_ClampCoord(texture.size, coord);
    i32 i = Tex_CoordToIndex(texture.size, coord);
    Tex_Writei1(texture, i, value);
}

pim_inline void VEC_CALL Tex_Writef2(
    texture_t texture, float2 uv, float4 value)
{
    Tex_Writei2(texture, Tex_UvToCoord(texture.size, uv), value);
}

PIM_C_END