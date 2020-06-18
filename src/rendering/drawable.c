#include "rendering/drawable.h"
#include "allocator/allocator.h"
#include "common/find.h"
#include "threading/task.h"
#include "math/float4x4_funcs.h"
#include "rendering/camera.h"
#include "math/frustum.h"
#include "rendering/constants.h"
#include "rendering/tile.h"
#include "common/profiler.h"
#include "rendering/sampler.h"
#include "math/sampling.h"
#include "rendering/path_tracer.h"
#include "rendering/lightmap.h"
#include <string.h>

static drawables_t ms_drawables;

drawables_t* drawables_get(void) { return &ms_drawables; }

i32 drawables_add(u32 name)
{
    const i32 back = ms_drawables.count;
    const i32 len = back + 1;
    ms_drawables.count = len;

    PermGrow(ms_drawables.names, len);
    PermGrow(ms_drawables.meshes, len);
    PermGrow(ms_drawables.materials, len);
    PermGrow(ms_drawables.lmUvs, len);
    PermGrow(ms_drawables.bounds, len);
    PermGrow(ms_drawables.tileMasks, len);
    PermGrow(ms_drawables.matrices, len);
    PermGrow(ms_drawables.translations, len);
    PermGrow(ms_drawables.rotations, len);
    PermGrow(ms_drawables.scales, len);

    ms_drawables.names[back] = name;
    ms_drawables.translations[back] = f4_0;
    ms_drawables.scales[back] = f4_1;
    ms_drawables.rotations[back] = quat_id;
    ms_drawables.matrices[back] = f4x4_id;

    return back;
}

static void DestroyAtIndex(i32 i)
{
    ASSERT(i >= 0);
    ASSERT(i < ms_drawables.count);
    mesh_release(ms_drawables.meshes[i]);
    material_t material = ms_drawables.materials[i];
    texture_release(material.albedo);
    texture_release(material.rome);
    texture_release(material.normal);
    lm_uvs_del(ms_drawables.lmUvs + i);
}

static void RemoveAtIndex(i32 i)
{
    DestroyAtIndex(i);

    const i32 len = ms_drawables.count;
    ms_drawables.count = len - 1;
    ASSERT(len > 0);

    PopSwap(ms_drawables.names, i, len);
    PopSwap(ms_drawables.meshes, i, len);
    PopSwap(ms_drawables.materials, i, len);
    PopSwap(ms_drawables.lmUvs, i, len);
    PopSwap(ms_drawables.bounds, i, len);
    PopSwap(ms_drawables.tileMasks, i, len);
    PopSwap(ms_drawables.matrices, i, len);
    PopSwap(ms_drawables.translations, i, len);
    PopSwap(ms_drawables.rotations, i, len);
    PopSwap(ms_drawables.scales, i, len);
}

bool drawables_rm(u32 name)
{
    const i32 i = drawables_find(name);
    if (i == -1)
    {
        return false;
    }
    RemoveAtIndex(i);
    return true;
}

i32 drawables_find(u32 name)
{
    return Find_u32(ms_drawables.names, ms_drawables.count, name);
}

void drawables_clear(void)
{
    i32 len = ms_drawables.count;
    for (i32 i = 0; i < len; ++i)
    {
        DestroyAtIndex(i);
    }
    ms_drawables.count = 0;
}

typedef struct trstask_s
{
    task_t task;
} trstask_t;

static void TRSFn(task_t* pBase, i32 begin, i32 end)
{
    trstask_t* pTask = (trstask_t*)pBase;
    const float4* pim_noalias translations = ms_drawables.translations;
    const quat* pim_noalias rotations = ms_drawables.rotations;
    const float4* pim_noalias scales = ms_drawables.scales;
    float4x4* pim_noalias matrices = ms_drawables.matrices;

    for (i32 i = begin; i < end; ++i)
    {
        matrices[i] = f4x4_trs(translations[i], rotations[i], scales[i]);
    }
}

ProfileMark(pm_TRS, drawables_trs)
void drawables_trs(void)
{
    ProfileBegin(pm_TRS);

    trstask_t* task = tmp_calloc(sizeof(*task));
    task_run(&task->task, TRSFn, ms_drawables.count);

    ProfileEnd(pm_TRS);
}

typedef struct boundstask_s
{
    task_t task;
} boundstask_t;

static void BoundsFn(task_t* pBase, i32 begin, i32 end)
{
    const meshid_t* pim_noalias meshes = ms_drawables.meshes;
    const float4* pim_noalias translations = ms_drawables.translations;
    const float4* pim_noalias scales = ms_drawables.scales;
    sphere_t* pim_noalias bounds = ms_drawables.bounds;
    u64* pim_noalias tileMasks = ms_drawables.tileMasks;

    mesh_t mesh;
    for (i32 i = begin; i < end; ++i)
    {
        sphere_t b = { 0 };
        u64 tilemask = 0;
        if (mesh_get(meshes[i], &mesh))
        {
            b = sphTransform(mesh.bounds, translations[i], scales[i]);
            tilemask = 1;
        }
        bounds[i] = b;
        tileMasks[i] = tilemask;
    }
}

ProfileMark(pm_Bounds, drawables_bounds)
void drawables_bounds(void)
{
    ProfileBegin(pm_Bounds);

    boundstask_t* task = tmp_calloc(sizeof(*task));
    task_run(&task->task, BoundsFn, ms_drawables.count);

    ProfileEnd(pm_Bounds);
}

typedef struct culltask_s
{
    task_t task;
    frus_t frus;
    frus_t subfrus[kTileCount];
    plane_t fwdPlane;
    float4 eye;
    const framebuf_t* backBuf;
} culltask_t;

SASSERT((sizeof(u64) * 8) == kTileCount);

static bool DepthCullTest(
    const float* pim_noalias depthBuf,
    int2 size,
    i32 iTile,
    float4 eye,
    sphere_t sph)
{
    const float radius = sph.value.w;
    const float4 rd = f4_normalize3(f4_sub(sph.value, eye));
    const float t = isectSphere3D((ray_t) { eye, rd }, sph);
    if (t == -1.0f)
    {
        return false;
    }
    if (t < -radius)
    {
        return false;
    }

    const int2 tile = GetTile(iTile);
    for (i32 y = 0; y < kTileHeight; ++y)
    {
        for (i32 x = 0; x < kTileWidth; ++x)
        {
            i32 sy = y + tile.y;
            i32 sx = x + tile.x;
            i32 i = sx + sy * kDrawWidth;
            if (t <= depthBuf[i])
            {
                return true;
            }
        }
    }

    return false;
}

static void CullFn(task_t* pBase, i32 begin, i32 end)
{
    culltask_t* pTask = (culltask_t*)pBase;
    const frus_t frus = pTask->frus;
    const framebuf_t* backBuf = pTask->backBuf;
    const frus_t* pim_noalias subfrus = pTask->subfrus;
    const sphere_t* pim_noalias bounds = ms_drawables.bounds;
    const float* pim_noalias depthBuf = backBuf->depth;
    const int2 bufSize = { backBuf->width, backBuf->height };
    u64* pim_noalias tileMasks = ms_drawables.tileMasks;

    camera_t camera;
    camera_get(&camera);
    float4 eye = camera.position;

    for (i32 i = begin; i < end; ++i)
    {
        if (!tileMasks[i])
        {
            continue;
        }

        u64 tilemask = 0;
        const sphere_t sphWS = bounds[i];
        float frusDist = sdFrusSph(frus, sphWS);
        if (frusDist <= 0.0f)
        {
            for (i32 iTile = 0; iTile < kTileCount; ++iTile)
            {
                float subFrusDist = sdFrusSph(subfrus[iTile], sphWS);
                if (subFrusDist <= 0.0f)
                {
                    if (DepthCullTest(depthBuf, bufSize, iTile, eye, sphWS))
                    {
                        u64 mask = 1;
                        mask <<= iTile;
                        tilemask |= mask;
                    }
                }
            }
        }
        tileMasks[i] = tilemask;
    }
}

ProfileMark(pm_Cull, drawables_cull)
void drawables_cull(
    const camera_t* camera,
    const framebuf_t* backBuf)
{
    ProfileBegin(pm_Cull);
    ASSERT(camera);

    culltask_t* task = tmp_calloc(sizeof(*task));
    task->backBuf = backBuf;

    camera_frustum(camera, &(task->frus));
    for (i32 t = 0; t < kTileCount; ++t)
    {
        int2 tile = GetTile(t);
        camera_subfrustum(camera, &(task->subfrus[t]), TileMin(tile), TileMax(tile));
    }

    float4 fwd = quat_fwd(camera->rotation);
    fwd.w = f4_dot3(fwd, camera->position);
    task->fwdPlane.value = fwd;
    task->eye = camera->position;

    task_run(&task->task, CullFn, ms_drawables.count);

    ProfileEnd(pm_Cull);
}
