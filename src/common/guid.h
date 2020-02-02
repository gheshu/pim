#pragma once

#include "common/macro.h"
#include "common/int_types.h"
#include "common/hash.h"
#include "common/comparator.h"
#include "common/random.h"

struct Guid
{
    u64 a;
    u64 b;
};

static bool GuidEqualsFn(const Guid& lhs, const Guid& rhs)
{
    return !((lhs.a - rhs.a) | (lhs.b - rhs.b));
}

static i32 GuidCompareFn(const Guid& lhs, const Guid& rhs)
{
    if (lhs.a != rhs.a)
    {
        return lhs.a < rhs.a ? -1 : 1;
    }
    if (lhs.b != rhs.b)
    {
        return lhs.b < rhs.b ? -1 : 1;
    }
    return 0;
}

static u32 GuidHashFn(const Guid& x)
{
    return Fnv32Qword(x.b, Fnv32Qword(x.a));
}

static constexpr Comparator<Guid> GuidComparator = { GuidEqualsFn, GuidCompareFn, GuidHashFn };

inline constexpr bool IsNull(Guid x)
{
    return !(x.a | x.b);
}

inline constexpr Guid ToGuid(cstrc str, u64 seed = Fnv64Bias)
{
    ASSERT(str);
    u64 a = Fnv64String(str, seed);
    a = a ? a : 1;
    u64 b = Fnv64String(str, a);
    b = b ? b : 1;
    return Guid{ a, b };
}

inline Guid ToGuid(const void* ptr, i32 count, u64 seed = Fnv64Bias)
{
    ASSERT(ptr);
    u64 a = Fnv64Bytes(ptr, count, seed);
    a = a ? a : 1;
    u64 b = Fnv64Bytes(ptr, count, a);
    b = b ? b : 1;
    return Guid{ a, b };
}

inline Guid CreateGuid()
{
    u64 a = Random::NextU64();
    u64 b = Random::NextU64();
    a = a ? a : 1;
    b = b ? b : 1;
    return Guid{ a, b };
}
