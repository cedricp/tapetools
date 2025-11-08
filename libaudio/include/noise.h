#pragma once

#include <stdint.h>
#include <math.h>

static bool is_seed_init = false;
static uint64_t u,v,w;

static inline uint64_t randq64_uint64()
{    
    if (!is_seed_init){
        is_seed_init = true;
        v = 4101842887655102017LL;
        w = 1;
        u = 1ULL ^ v; 
        randq64_uint64();
        v = u; 
        randq64_uint64();
        w = v; 
        randq64_uint64();
    }
    
	u = u * 2862933555777941757LL + 7046029254386353087LL;
	v ^= v >> 17; 
    v ^= v << 31; 
    v ^= v >> 8;
	w = 4294957665U*(w & 0xffffffff) + (w >> 32);
	uint64_t x = u ^ (u << 21); 
    x ^= x >> 35; 
    x ^= x << 4;
	return (x + v) ^ w;
}

static inline double randq64_double()
{
    return 5.42101086242752217E-20 * randq64_uint64();
}