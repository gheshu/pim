#pragma once

#include "common/macro.h"

PIM_C_BEGIN

#include "threading/semaphore.h"

typedef struct event_s
{
    i32 state;
    semaphore_t sema;
} event_t;

void event_create(event_t* evt);
void event_destroy(event_t* evt);
void event_wait(event_t* evt);
void event_wakeone(event_t* evt);
void event_wakeall(event_t* evt);

PIM_C_END
