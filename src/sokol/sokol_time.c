#include "common/macro.h"
#include "allocator/allocator.h"
#include "io/fd.h"

#define SOKOL_ASSERT(c)     ASSERT(c)
#define SOKOL_LOG(msg)      fd_puts(fd_stderr, msg)
#define SOKOL_MALLOC(s)     perm_malloc(s)
#define SOKOL_FREE(p)       pim_free(p)
#define SOKOL_IMPL          1

#include <sokol/sokol_time.h>
