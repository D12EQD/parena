#pragma once
#include "def.h"

__attribute__((constructor))
void init_rand_state();

u32 get_random();

