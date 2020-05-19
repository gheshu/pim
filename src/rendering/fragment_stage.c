#include "rendering/fragment_stage.h"
#include "allocator/allocator.h"
#include "common/atomics.h"
#include "common/profiler.h"
#include "common/cvar.h"
#include "threading/task.h"

#include "math/types.h"
#include "math/float4x4_funcs.h"
#include "math/float4_funcs.h"
#include "math/float3_funcs.h"
#include "math/lighting.h"
#include "math/color.h"
#include "math/frustum.h"
#include "math/sdf.h"
#include "math/sphgauss.h"

#include "rendering/sampler.h"
#include "rendering/tile.h"
#include "rendering/mesh.h"
#include "rendering/texture.h"
#include "rendering/material.h"
#include "rendering/camera.h"
#include "rendering/framebuffer.h"
#include "rendering/lights.h"

#include "components/table.h"
#include "components/drawables.h"
#include "components/components.h"

static cvar_t cv_sg_dbg_diff = { cvart_bool, 0, "sg_dbg_diff", "0", "display a debug view of diffuse GI" };
static cvar_t cv_sg_dbg_spec = { cvart_bool, 0, "sg_dbg_spec", "0", "display a debug view of specular GI" };

typedef struct fragstage_s
{
    task_t task;
    framebuf_t* frontBuf;
    const framebuf_t* backBuf;
    table_t* table;
} fragstage_t;

typedef struct tile_ctx_s
{
    frus_t frus;
    float4x4 VP;
    float4 tileNormal;
    float4 eye;
    float4 right;
    float4 up;
    float4 fwd;
    float2 slope;
    float2 tileMin;
    float2 tileMax;
    float nearClip;
    float farClip;
    float tileDepth;
} tile_ctx_t;

static BrdfLut ms_lut;
static float4 ms_diffuseGI;
static float4 ms_specularGI;

#define MAX_SGS 256
static i32 ms_sgcount;
static SG_t ms_sgs[MAX_SGS];
static float ms_sgintegrals[MAX_SGS];
static float ms_sgweights[MAX_SGS];

SG_t* SG_Get(void) { return ms_sgs; }
float* SG_GetIntegrals(void) { return ms_sgintegrals; }
float* SG_GetWeights(void) { return ms_sgweights; }
i32 SG_GetCount(void) { return ms_sgcount; }
void SG_SetCount(i32 ct) { ms_sgcount = i1_clamp(ct, 0, MAX_SGS); }
float4* DiffuseGI(void) { return &ms_diffuseGI; }
float4* SpecularGI(void) { return &ms_specularGI; }

static void EnsureInit(void);
static void SetupTile(tile_ctx_t* ctx, i32 iTile, const framebuf_t* backBuf);
static float4 VEC_CALL TriBounds(float4x4 VP, float4 A, float4 B, float4 C, float2 tileMin, float2 tileMax);
static void VEC_CALL DrawMesh(const tile_ctx_t* ctx, framebuf_t* target, const drawable_t* drawable);

pim_optimize
static void FragmentStageFn(task_t* task, i32 begin, i32 end)
{
    fragstage_t* stage = (fragstage_t*)task;

    table_t* table = stage->table;
    ASSERT(table);
    const i32 drawCount = table_width(table);
    const drawable_t* drawables = table_row(table, TYPE_ARGS(drawable_t));

    tile_ctx_t ctx;
    for (i32 i = begin; i < end; ++i)
    {
        SetupTile(&ctx, i, stage->backBuf);

        u64 tilemask = 1;
        tilemask <<= i;

        for (i32 j = 0; j < drawCount; ++j)
        {
            if (drawables[j].tilemask & tilemask)
            {
                DrawMesh(&ctx, stage->frontBuf, drawables + j);
            }
        }
    }
}

static void EnsureInit(void)
{
    if (!ms_lut.texels)
    {
        cvar_reg(&cv_sg_dbg_diff);
        cvar_reg(&cv_sg_dbg_spec);

        ms_lut = BakeBRDF(i2_s(256), 1024);
    }
}

ProfileMark(pm_FragmentStage, Drawables_Fragment)
task_t* Drawables_Fragment(struct tables_s* tables, struct framebuf_s* frontBuf, const struct framebuf_s* backBuf)
{
    ProfileBegin(pm_FragmentStage);
    ASSERT(tables);
    ASSERT(frontBuf);
    ASSERT(backBuf);

    task_t* result = NULL;

    EnsureInit();

    table_t* table = Drawables_Get(tables);
    if (table)
    {
        fragstage_t* stage = tmp_calloc(sizeof(*stage));
        stage->frontBuf = frontBuf;
        stage->backBuf = backBuf;
        stage->table = table;
        task_submit((task_t*)stage, FragmentStageFn, kTileCount);
        result = (task_t*)stage;
    }

    ProfileEnd(pm_FragmentStage);
    return result;
}

pim_optimize
static float4 VEC_CALL TriBounds(float4x4 VP, float4 A, float4 B, float4 C, float2 tileMin, float2 tileMax)
{
    A = f4x4_mul_pt(VP, A);
    B = f4x4_mul_pt(VP, B);
    C = f4x4_mul_pt(VP, C);

    A = f4_divvs(A, A.w);
    B = f4_divvs(B, B.w);
    C = f4_divvs(C, C.w);

    float4 bounds;
    bounds.x = f1_min(A.x, f1_min(B.x, C.x));
    bounds.y = f1_min(A.y, f1_min(B.y, C.y));
    bounds.z = f1_max(A.x, f1_max(B.x, C.x));
    bounds.w = f1_max(A.y, f1_max(B.y, C.y));

    bounds.x = f1_max(bounds.x, tileMin.x);
    bounds.y = f1_max(bounds.y, tileMin.y);
    bounds.z = f1_min(bounds.z, tileMax.x);
    bounds.w = f1_min(bounds.w, tileMax.y);

    return bounds;
}

pim_optimize
static void SetupTile(tile_ctx_t* ctx, i32 iTile, const framebuf_t* backBuf)
{
    camera_t camera;
    camera_get(&camera);

    const int2 tile = GetTile(iTile);
    ctx->nearClip = camera.nearFar.x;
    ctx->farClip = camera.nearFar.y;
    ctx->slope = proj_slope(f1_radians(camera.fovy), kDrawAspect);

    ctx->fwd = quat_fwd(camera.rotation);
    ctx->right = quat_right(camera.rotation);
    ctx->up = quat_up(camera.rotation);
    ctx->eye = camera.position;

    ctx->tileMin = TileMin(tile);
    ctx->tileMax = TileMax(tile);
    ctx->tileDepth = TileDepth(backBuf, tile);
    ctx->tileNormal = proj_dir(ctx->right, ctx->up, ctx->fwd, ctx->slope, f2_lerp(ctx->tileMin, ctx->tileMax, 0.5f));
    ctx->frus = frus_new(ctx->eye, ctx->right, ctx->up, ctx->fwd, ctx->tileMin, ctx->tileMax, ctx->slope, camera.nearFar);

    float4x4 V = f4x4_lookat(ctx->eye, f4_add(ctx->eye, ctx->fwd), ctx->up);
    float4x4 P = f4x4_perspective(f1_radians(camera.fovy), kDrawAspect, ctx->nearClip, ctx->farClip);
    ctx->VP = f4x4_mul(P, V);
}

pim_optimize
static void VEC_CALL DrawMesh(const tile_ctx_t* ctx, framebuf_t* target, const drawable_t* drawable)
{
    mesh_t mesh;
    texture_t albedoMap = { 0 };
    texture_t romeMap = { 0 };
    if (!mesh_get(drawable->tmpmesh, &mesh))
    {
        return;
    }

    const bool dbgdiffGI = cv_sg_dbg_diff.asFloat != 0.0f;
    const bool dbgspecGI = cv_sg_dbg_spec.asFloat != 0.0f;

    const float dx = 1.0f / kDrawWidth;
    const float dy = 1.0f / kDrawHeight;
    const float e = 1.0f / (1 << 10);

    const BrdfLut lut = ms_lut;
    const float4 flatAlbedo = ColorToLinear(drawable->material.flatAlbedo);
    const float4 flatRome = ColorToLinear(drawable->material.flatRome);
    texture_get(drawable->material.albedo, &albedoMap);
    texture_get(drawable->material.rome, &romeMap);

    const float4 eye = ctx->eye;
    const float4 fwd = ctx->fwd;
    const float4 right = ctx->right;
    const float4 up = ctx->up;
    const float2 slope = ctx->slope;
    const float nearClip = ctx->nearClip;
    const float farClip = ctx->farClip;
    const float4 tileNormal = ctx->tileNormal;
    const float2 tileMin = ctx->tileMin;
    const float2 tileMax = ctx->tileMax;
    const float tileDepth = ctx->tileDepth;
    plane_t fwdPlane;
    fwdPlane.value = fwd;
    fwdPlane.value.w = f4_dot3(fwd, eye);

    const lights_t* lights = lights_get();
    const i32 dirCount = lights->dirCount;
    const dir_light_t* pim_noalias dirLights = lights->dirLights;
    const i32 ptCount = lights->ptCount;
    const pt_light_t* pim_noalias ptLights = lights->ptLights;

    float4* pim_noalias dstLight = target->light;
    float* pim_noalias dstDepth = target->depth;

    const float4* pim_noalias positions = mesh.positions;
    const float4* pim_noalias normals = mesh.normals;
    const float2* pim_noalias uvs = mesh.uvs;
    const i32 vertCount = mesh.length;

    for (i32 iVert = 0; (iVert + 3) <= vertCount; iVert += 3)
    {
        const float4 A = positions[iVert + 0];
        const float4 B = positions[iVert + 1];
        const float4 C = positions[iVert + 2];

        const float4 BA = f4_sub(B, A);
        const float4 CA = f4_sub(C, A);

        {
            // backface culling
            if (f4_dot3(tileNormal, f4_cross3(CA, BA)) < 0.0f)
            {
                continue;
            }
            const sphere_t sph = triToSphere(A, B, C);
            // occlusion culling
            if (sdPlaneSphere(fwdPlane, sph) > tileDepth)
            {
                continue;
            }
            // tile-frustum-triangle culling
            if (sdFrusSph(ctx->frus, sph) > 0.0f)
            {
                continue;
            }
        }

        const float4 NA = normals[iVert + 0];
        const float4 NB = normals[iVert + 1];
        const float4 NC = normals[iVert + 2];

        const float2 UA = uvs[iVert + 0];
        const float2 UB = uvs[iVert + 1];
        const float2 UC = uvs[iVert + 2];

        const float4 T = f4_sub(eye, A);
        const float4 Q = f4_cross3(T, BA);
        const float t0 = f4_dot3(CA, Q);

        // bounds is broken by triangle clipping / clip space
        const float4 bounds = TriBounds(ctx->VP, A, B, C, tileMin, tileMax);
        for (float y = bounds.y; y < bounds.w; y += dy)
        {
            for (float x = bounds.x; x < bounds.z; x += dx)
            {
                const float2 coord = { x, y };
                const float4 rd = proj_dir(right, up, fwd, slope, coord);
                const float4 rdXca = f4_cross3(rd, CA);
                const float det = f4_dot3(BA, rdXca);
                if (det < e)
                {
                    continue;
                }

                const i32 iTexel = SnormToIndex(coord);
                float4 wuvt;
                {
                    // barycentric and depth clipping
                    const float rcpDet = 1.0f / det;
                    float t = t0 * rcpDet;
                    if ((t < nearClip) || (t > dstDepth[iTexel]))
                    {
                        continue;
                    }
                    wuvt.y = f4_dot3(T, rdXca) * rcpDet;
                    wuvt.z = f4_dot3(rd, Q) * rcpDet;
                    wuvt.x = 1.0f - wuvt.y - wuvt.z;
                    if ((wuvt.x < 0.0f) || (wuvt.y < 0.0f) || (wuvt.z < 0.0f))
                    {
                        continue;
                    }
                    dstDepth[iTexel] = t;
                }

                float4 lighting = f4_0;
                {
                    // blend interpolators
                    const float4 P = f4_blend(A, B, C, wuvt);
                    const float4 N = f4_normalize3(f4_blend(NA, NB, NC, wuvt));
                    const float2 U = f2_frac(f2_blend(UA, UB, UC, wuvt));

                    // lighting
                    const float4 V = f4_normalize3(f4_sub(eye, P));
                    const float4 R = f4_normalize3(f4_reflect(f4_neg(V), N));
                    float4 albedo = flatAlbedo;
                    if (albedoMap.texels)
                    {
                        albedo = f4_mul(albedo, Tex_Bilinearf2(albedoMap, U));
                    }
                    float4 rome = flatRome;
                    if (romeMap.texels)
                    {
                        rome = f4_mul(rome, Tex_Bilinearf2(romeMap, U));
                    }
                    {
                        float4 diffuseGI = f4_0;
                        float4 specularGI = f4_0;
                        const i32 sgcount = ms_sgcount;
                        const SG_t* pim_noalias sgs = ms_sgs;
                        for (i32 i = 0; i < sgcount; ++i)
                        {
                            diffuseGI = f4_add(diffuseGI, SG_Irradiance(sgs[i], N));
                            specularGI = f4_add(specularGI, SG_Eval(sgs[i], R));
                        }
                        const float4 indirect = IndirectBRDF(lut, V, N, diffuseGI, specularGI, albedo, rome.x, rome.z, rome.y);
                        lighting = f4_add(lighting, indirect);
                    }
                    for (i32 iLight = 0; iLight < dirCount; ++iLight)
                    {
                        dir_light_t light = dirLights[iLight];
                        const float4 direct = DirectBRDF(V, light.dir, light.rad, N, albedo, rome.x, rome.z);
                        lighting = f4_add(lighting, direct);
                    }
                    for (i32 iLight = 0; iLight < ptCount; ++iLight)
                    {
                        pt_light_t light = ptLights[iLight];
                        float4 dir = f4_sub(light.pos, P);
                        float dist = f4_length3(dir);
                        dir = f4_divvs(dir, dist);
                        float attenuation = 1.0f / (0.01f + dist * dist);
                        light.rad = f4_mulvs(light.rad, attenuation);
                        const float4 direct = DirectBRDF(V, dir, light.rad, N, albedo, rome.x, rome.z);
                        lighting = f4_add(lighting, direct);
                    }

                    if (dbgdiffGI)
                    {
                        lighting = f4_0;
                        const i32 sgcount = ms_sgcount;
                        const SG_t* pim_noalias sgs = ms_sgs;
                        for (i32 i = 0; i < sgcount; ++i)
                        {
                            lighting = f4_add(lighting, SG_Irradiance(sgs[i], N));
                        }
                    }
                    else if (dbgspecGI)
                    {
                        lighting = f4_0;
                        const i32 sgcount = ms_sgcount;
                        const SG_t* pim_noalias sgs = ms_sgs;
                        for (i32 i = 0; i < sgcount; ++i)
                        {
                            lighting = f4_add(lighting, SG_Eval(sgs[i], R));
                        }
                    }
                }

                dstLight[iTexel] = lighting;
            }
        }
    }
}
