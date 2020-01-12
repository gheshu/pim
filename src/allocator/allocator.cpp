#include "allocator/allocator.h"

#include "allocator/allocators.h"
#include "containers/array.h"
#include "common/hashstring.h"
#include "common/round.h"
#include <stdlib.h>

#if ENABLE_LEAK_TRACKER
#include "allocator/leak_tracker.h"
#endif

namespace Allocator
{
    static constexpr i32 Alignment = 16;
    static constexpr i32 PadBytes = sizeof(Header);

    // Separate from 'VTables' so that we can have multiple of the same type.
    static constexpr VTable ms_tables[] =
    {
        Stdlib::Table,
        Linear::Table,
        Stack::Table,
        Pool::Table,
        Stdlib::Table,
    };
    static constexpr i32 NumAllocators = NELEM(ms_tables);

    // TODO: make these config vars (and make a config var system...)
    static constexpr i32 ms_capacities[] =
    {
        0,          // stdlib doesnt care
        1 << 20,    // 1 mb for linear
        1 << 20,    // 1 mb for stack
        64 << 20,   // 64 mb for pool
        0,          // debug uses stdlib
    };
    SASSERT(NELEM(ms_capacities) == NumAllocators);

    static constexpr bool ms_isPerFrame[] =
    {
        false,
        true,
        false,
        false,
        false,
    };
    SASSERT(NELEM(ms_isPerFrame) == NumAllocators);

    // ------------------------------------------------------------------------

    static UAllocator ms_allocators[NumAllocators];
    static void* ms_allocations[NumAllocators];
    static bool ms_init;

#if ENABLE_LEAK_TRACKER
    static LeakTracker ms_tracker;
#endif // 

    // ------------------------------------------------------------------------

    static constexpr bool IsAligned(usize x)
    {
        constexpr usize mask = (usize)(Alignment - 1);
        return !(x & mask);
    }
    static constexpr bool IsAligned(void* ptr)
    {
        return IsAligned((usize)ptr);
    }

    SASSERT(IsAligned(PadBytes));

    static bool InRange(i32 iType)
    {
        return (u32)iType < (u32)NumAllocators;
    }

    static bool ValidAllocator(i32 iType)
    {
        return InRange(iType) && (ms_init || (iType == Alloc_Stdlib) || (iType == Alloc_Debug));
    }

    static i32 PadRequest(i32 reqBytes)
    {
        reqBytes += PadBytes;
        reqBytes = MultipleOf(reqBytes, Alignment);
        ASSERT(reqBytes > 0);
        return reqBytes;
    }

    // ------------------------------------------------------------------------

    void Init()
    {
        ASSERT(!ms_init);
        for (i32 i = 0; i < NumAllocators; ++i)
        {
            i32 capacity = ms_capacities[i];
            void* memory = capacity > 0 ? malloc(capacity) : 0;
            ms_allocations[i] = memory;
            ms_tables[i].Init(ms_allocators[i], memory, capacity);
        }
        ms_init = true;
    }

    void Update()
    {
        ASSERT(ms_init);
        for (i32 i = 0; i < NumAllocators; ++i)
        {
            if (ms_isPerFrame[i])
            {
                ms_tables[i].Clear(ms_allocators[i]);
            }
        }
    }

    void Shutdown()
    {
        ASSERT(ms_init);
        for (i32 i = 0; i < NumAllocators; ++i)
        {
            ms_tables[i].Shutdown(ms_allocators[i]);
            void* memory = ms_allocations[i];
            ms_allocations[i] = 0;
            if (memory)
            {
                free(memory);
            }
        }
        ms_init = false;

#if ENABLE_LEAK_TRACKER
        ms_tracker.ListLeaks();
#endif
    }

    // ------------------------------------------------------------------------

    void* Alloc(AllocType type, i32 want)
    {
        ASSERT(ValidAllocator(type));
        ASSERT(want >= 0);

        if (want > 0)
        {
            want = PadRequest(want);

            Header* hdr = ms_tables[type].Alloc(ms_allocators[type], want);

            ASSERT(hdr);
            hdr->size = want;
            hdr->type = type;

            ASSERT(IsAligned(hdr + 1));

#if ENABLE_LEAK_TRACKER
            if (type != Alloc_Debug)
            {
                ms_tracker.OnAlloc(hdr, want);
            }
#endif

            return hdr + 1;
        }
        return 0;
    }

    void* Realloc(AllocType type, void* prev, i32 want)
    {
        ASSERT(ValidAllocator(type));
        ASSERT(want >= 0);

        if (!prev)
        {
            return Alloc(type, want);
        }

        if (!want)
        {
            Free(prev);
            return 0;
        }

        Header* hdr = ((Header*)prev) - 1;

        const i32 has = hdr->size - PadBytes;
        ASSERT(has > 0);

        if (want != has)
        {
            want = PadRequest(want);

            const i32 iType = hdr->type;
            ASSERT(ValidAllocator(iType));
            ASSERT(ms_init || iType == Alloc_Stdlib);

            Header* newHdr = ms_tables[iType].Realloc(
                ms_allocators[iType],
                hdr,
                want);

            ASSERT(newHdr);
            newHdr->size = want;
            newHdr->type = iType;

            ASSERT(IsAligned(newHdr + 1));

#if ENABLE_LEAK_TRACKER
            if (iType != Alloc_Debug)
            {
                ms_tracker.OnRealloc(hdr, newHdr, want);
            }
#endif

            return newHdr + 1;
        }

        return prev;
    }

    void Free(void* prev)
    {
        if (prev)
        {
            ASSERT(IsAligned(prev));

            Header* hdr = ((Header*)prev) - 1;

            const i32 size = hdr->size;
            ASSERT(size > 0);

            const i32 iType = hdr->type;
            ASSERT(ValidAllocator(iType));

            ms_tables[iType].Free(ms_allocators[iType], hdr);

#if ENABLE_LEAK_TRACKER
            if (iType != Alloc_Debug)
            {
                ms_tracker.OnFree(hdr);
            }
#endif
        }
    }
};
