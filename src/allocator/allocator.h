#pragma once

#include "common/macro.h"

PIM_C_BEGIN

void alloc_sys_init(void);
void alloc_sys_update(void);
void alloc_sys_shutdown(void);

void* pim_malloc(EAlloc allocator, i32 bytes);
void pim_free(void* ptr);
void* pim_realloc(EAlloc allocator, void* prev, i32 bytes);
void* pim_calloc(EAlloc allocator, i32 bytes);

// alternative to alloca
void* pim_pusha(i32 bytes);
void pim_popa(i32 bytes);

inline void* perm_malloc(i32 bytes) { return pim_malloc(EAlloc_Perm, bytes); }
inline void* perm_calloc(i32 bytes) { return pim_calloc(EAlloc_Perm, bytes); }
inline void* perm_realloc(void* prev, i32 bytes) { return pim_realloc(EAlloc_Perm, prev, bytes); }

inline void* tex_malloc(i32 bytes) { return pim_malloc(EAlloc_Texture, bytes); }
inline void* tex_calloc(i32 bytes) { return pim_calloc(EAlloc_Texture, bytes); }
inline void* tex_realloc(void* prev, i32 bytes) { return pim_realloc(EAlloc_Texture, prev, bytes); }

inline void* tmp_malloc(i32 bytes) { return pim_malloc(EAlloc_Temp, bytes); }
inline void* tmp_realloc(void* prev, i32 bytes) { return pim_realloc(EAlloc_Temp, prev, bytes); }
inline void* tmp_calloc(i32 bytes) { return pim_calloc(EAlloc_Temp, bytes); }

#define FreePtr(ptr) do { pim_free(ptr); (ptr) = NULL; } while(0)

#define ZeroElem(ptr, i)        do { memset((ptr) + (i), 0, sizeof((ptr)[0])); } while(0)
#define PopSwap(ptr, i, len)    do { memcpy((ptr) + (i), (ptr) + (len) - 1, sizeof((ptr)[0])); } while(0)
#define PermReserve(ptr, len)   do { (ptr) = perm_realloc((ptr), sizeof((ptr)[0]) * (len)); } while(0)
#define PermGrow(ptr, len)      do { PermReserve(ptr, len); ZeroElem(ptr, (len) - 1); } while(0)

#define TempReserve(ptr, len)   do { (ptr) = tmp_realloc((ptr), sizeof((ptr)[0]) * (len)); } while(0)

PIM_C_END
