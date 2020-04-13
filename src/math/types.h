#pragma once

#include "common/macro.h"

PIM_C_BEGIN

typedef pim_alignas(8) struct float2_s
{
    float x;
    float y;
} float2;

typedef struct float2x2_s
{
    float2 c0;
    float2 c1;
} float2x2;

typedef struct float3_s
{
    float x;
    float y;
    float z;
} float3;

typedef struct float3x3_s
{
    float3 c0;
    float3 c1;
    float3 c2;
} float3x3;

typedef pim_alignas(16) struct float4_s
{
    float x;
    float y;
    float z;
    float w;
} float4;

typedef struct float4x4_s
{
    float4 c0;
    float4 c1;
    float4 c2;
    float4 c3;
} float4x4;

typedef struct quat_s
{
    float4 v;
} quat;

typedef pim_alignas(8) struct int2_s
{
    i32 x;
    i32 y;
} int2;

typedef struct int2x2_s
{
    int2 c0;
    int2 c1;
} int2x2;

typedef struct int3_s
{
    i32 x;
    i32 y;
    i32 z;
} int3;

typedef struct int3x3_s
{
    int3 c0;
    int3 c1;
    int3 c2;
} int3x3;

typedef pim_alignas(16) struct int4_s
{
    i32 x;
    i32 y;
    i32 z;
    i32 w;
} int4;

typedef struct int4x4_s
{
    int4 c0;
    int4 c1;
    int4 c2;
    int4 c3;
} int4x4;

PIM_C_END