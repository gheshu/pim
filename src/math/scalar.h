#pragma once

#include "common/macro.h"
#include <math.h>

#ifdef min
    #undef min
#endif // min

#ifdef max
    #undef max
#endif // max

#define kPi                 3.1415926535897932384626433832795f
#define kTau                6.283185307179586476925286766559f
#define kRadiansPerDegree   (kTau / 360.0f)
#define kDegreesPerRadian   (360.0f / kTau)
#define kEpsilon            (1.0f / (1<<22))
#define kGoldenRatio        1.6180339887498948482045868343656f // (1 + sqrt(5)) / 2
#define kSilverRatio        0.6180339887498948482045868343656f // (sqrt(5) - 1) / 2
#define kSqrt2              1.4142135623730950488016887242097f
#define kSqrt3              1.7320508075688772935274463415059f
#define kSqrt5              2.2360679774997896964091736687313f
#define kSqrt7              2.6457513110645905905016157536393f
#define kSqrt11             3.3166247903553998491149327366707f
#define kSqrt13             3.6055512754639892931192212674705f
#define kSqrt17             4.1231056256176605498214098559741f
#define kSqrt19             4.3588989435406735522369819838596f

// SI units
#define kKilo               1000.0f
#define kMeter              1.0f
#define kCenti              0.01f
#define kMilli              0.001f
#define kMicro              1e-6f
#define kNano               1e-9f

PIM_C_BEGIN

pim_inline float VEC_CALL f1_radians(float x)
{
    return x * kRadiansPerDegree;
}

pim_inline float VEC_CALL f1_degrees(float x)
{
    return x * kDegreesPerRadian;
}

pim_inline float VEC_CALL f1_sq(float x)
{
    return x * x;
}

pim_inline float VEC_CALL f1_unorm(float s)
{
    return 0.5f + 0.5f * s;
}

pim_inline float VEC_CALL f1_snorm(float u)
{
    return 2.0f * u - 1.0f;
}

pim_inline float VEC_CALL f1_min(float a, float b)
{
    return a < b ? a : b;
}

pim_inline float VEC_CALL f1_max(float a, float b)
{
    return a > b ? a : b;
}

pim_inline float VEC_CALL f1_divsafe(float a, float b)
{
    return a / f1_max(kEpsilon, b);
}

pim_inline float VEC_CALL f1_sign(float x)
{
    return ((x > 0.0f) ? 1.0f : 0.0f) - ((x < 0.0f) ? 1.0f : 0.0f);
}

pim_inline float VEC_CALL f1_clamp(float x, float lo, float hi)
{
    return f1_min(hi, f1_max(lo, x));
}

#define f1_sat(x) f1_saturate(x)
pim_inline float VEC_CALL f1_saturate(float x)
{
    return f1_clamp(x, 0.0f, 1.0f);
}

pim_inline float VEC_CALL f1_abs(float x)
{
    return f1_max(x, -x);
}

pim_inline float VEC_CALL f1_trunc(float x)
{
    i32 i = (i32)x;
    return (float)i;
}

pim_inline float VEC_CALL f1_frac(float x)
{
    return x - f1_trunc(x);
}

pim_inline float VEC_CALL f1_floor(float x)
{
    float i = f1_trunc(x);
    return i - (i > x);
}

pim_inline float VEC_CALL f1_ceil(float x)
{
    float i = f1_trunc(x);
    return i + (x > i);
}

pim_inline float VEC_CALL f1_round(float x)
{
    return (x >= 0.0f) ? f1_floor(x + 0.5f) : f1_ceil(x - 0.5f);
}

pim_inline float VEC_CALL f1_mod(float num, float div)
{
    return fmodf(num, div);
}

pim_inline float VEC_CALL f1_pow(float x, float n)
{
    return powf(x, n);
}

pim_inline float VEC_CALL f1_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

pim_inline float VEC_CALL f1_unlerp(float a, float b, float x)
{
    return f1_saturate((x - a) / (b - a));
}

pim_inline float VEC_CALL f1_qbezier(float a, float b, float c, float t)
{
    return f1_lerp(f1_lerp(a, b, t), f1_lerp(b, c, t), t);
}

pim_inline float VEC_CALL f1_select(float a, float b, float t)
{
    return (t != 0.0f) ? b : a;
}

pim_inline float VEC_CALL f1_step(float a, float b)
{
    return (a >= b) ? 1.0f : 0.0f;
}

// assumes x is already within [0, 1] range
pim_inline float VEC_CALL f1_unormstep(float t)
{
    return t * t * ((t * -2.0f) + 3.0f);
}

pim_inline float VEC_CALL f1_unormerstep(float t)
{
    return t * t * t * (t * (t * 6.0f + -15.0f) + 10.0f);
}

pim_inline float VEC_CALL f1_smoothstep(float a, float b, float x)
{
    return f1_unormstep(f1_unlerp(a, b, x));
}

pim_inline float VEC_CALL f1_smootherstep(float a, float b, float x)
{
    return f1_unormerstep(f1_unlerp(a, b, x));
}

pim_inline float VEC_CALL f1_wrap(float x)
{
    return (x > 1.0f) ? x - 1.0f : x;
}

pim_inline float VEC_CALL f1_reflect(float i, float n)
{
    return i - 2.0f * (n * i * n);
}

pim_inline float VEC_CALL f1_distance(float a, float b)
{
    return f1_abs(b - a);
}

pim_inline float VEC_CALL f1_sinc(float x)
{
    x = f1_abs(x);
    float xp = x * kPi;
    return sinf(xp) / xp;
}

pim_inline float VEC_CALL f1_wsinc(float x, float r, float t)
{
    x = f1_abs(x);
    if (x > r)
    {
        return 0.0f;
    }
    return f1_sinc(x) * f1_sinc(x / t);
}

// x: random variable
// u: mean
// s: standard deviation
// returns: P(x <= X <= x+dx), probability density of distribution X at x
pim_inline float VEC_CALL f1_gauss(float x, float u, float s)
{
    return expf(-0.5f * (f1_sq(x - u) / f1_sq(s))) / (s * 2.50662827463f);
}

// technically a positive-only half-gauss.
pim_inline float VEC_CALL f1_gauss_invcdf(float p, float u, float s)
{
    return u + s * sqrtf(-logf(1.0f - p));
}

// x: random variable
// u: mean
// s: standard deviation
// returns: P(x <= X <= x+dx), probability density of distribution X at x
// https://en.wikipedia.org/wiki/Logistic_distribution
// https://www.desmos.com/calculator/g8turex13k
pim_inline float VEC_CALL f1_logistic_pdf(float x, float u, float s)
{
    float t = expf(-(x - u) / s);
    return t / (s * f1_sq(1.0f + t));
}

// x: random variable
// u: mean
// s: standard deviation
// returns: P(X <= x), probability that distribution X is less than or equal x
pim_inline float VEC_CALL f1_logistic_cdf(float x, float u, float s)
{
    float t = expf(-(x - u) / s);
    return 1.0f / (1.0f + t);
}

// p: probability
// u: mean
// s: standard deviation
// returns: random variable x of distribution X with probability p
pim_inline float VEC_CALL f1_logistic_invcdf(float p, float u, float s)
{
    return u + s * logf(p / (1.0f - p));
}

pim_inline float VEC_CALL f1_cauchy_pdf(float x, float u, float s)
{
    float t = f1_sq((x - u) / s);
    return 1.0f / (kPi * s * (1.0f + t));
}

pim_inline float VEC_CALL f1_cauchy_invcdf(float p, float u, float s)
{
    return u + s * tanf(kPi * (p - 0.5f));
}

pim_inline i32 VEC_CALL i1_min(i32 a, i32 b)
{
    return a < b ? a : b;
}

pim_inline i32 VEC_CALL i1_max(i32 a, i32 b)
{
    return a > b ? a : b;
}

pim_inline i32 VEC_CALL i1_clamp(i32 x, i32 lo, i32 hi)
{
    return i1_min(hi, i1_max(lo, x));
}

pim_inline i32 VEC_CALL i1_abs(i32 x)
{
    return x & 0x7fffffff;
}

pim_inline i32 VEC_CALL i1_lerp(i32 a, i32 b, i32 t)
{
    return a + (b - a) * t;
}

pim_inline i32 VEC_CALL i1_distance(i32 a, i32 b)
{
    return i1_abs(b - a);
}

pim_inline i32 VEC_CALL i1_log2(i32 x)
{
    i32 y = -(x == 0);
    if (x >= (1 << 16))
    {
        y += 16;
        x >>= 16;
    }
    if (x >= (1 << 8))
    {
        y += 8;
        x >>= 8;
    }
    if (x >= (1 << 4))
    {
        y += 4;
        x >>= 4;
    }
    if (x >= (1 << 2))
    {
        y += 2;
        x >>= 2;
    }
    if (x >= (1 << 1))
    {
        y += 1;
        x >>= 1;
    }
    return y;
}

PIM_C_END
