// copy from https://en.wikipedia.org/wiki/Mersenne_Twister

#include <stdint.h>
#include <time.h>
#include "def.h"

#define n 624
#define m 397
#define w 32
#define r 31
#define UMASK (0xffffffffUL << r)
#define LMASK (0xffffffffUL >> (w-r))
#define a 0x9908b0dfUL
#define u 11
#define s 7
#define t 15
#define l 18
#define b 0x9d2c5680UL
#define c 0xefc60000UL
#define f 1812433253UL

typedef struct{
    uint32_t state_array[n];         // the array for the state vector 
    int state_index;                 // index into state vector array, 0 <= state_index <= n-1   always
} mt_state;

static mt_state g_rand_state;

__attribute__((constructor))
void init_rand_state() {
    u64 seed = time(0);
    uint32_t* state_array = &(g_rand_state.state_array[0]);
    state_array[0] = seed;                          // suggested initial seed = 19650218UL
    for (int i=1; i<n; i++)
    {
        seed = f * (seed ^ (seed >> (w-2))) + i;    // Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier.
        state_array[i] = seed; 
    }

    g_rand_state.state_index = 0;
}

u32 get_random() {
    uint32_t* state_array = &(g_rand_state.state_array[0]);

    int k = g_rand_state.state_index;
    int j = k - (n-1);               // point to state n-1 iterations before
    if (j < 0) j += n;               // modulo n circular indexing

    uint32_t x = (state_array[k] & UMASK) | (state_array[j] & LMASK);

    uint32_t xA = x >> 1;
    if (x & 0x00000001UL) xA ^= a;

    j = k - (n-m);                   // point to state n-m iterations before
    if (j < 0) j += n;               // modulo n circular indexing

    x = state_array[j] ^ xA;         // compute next value in the state
    state_array[k++] = x;            // update new state value

    if (k >= n) k = 0;               // modulo n circular indexing
    g_rand_state.state_index = k;

    uint32_t y = x ^ (x >> u);       // tempering 
    y = y ^ ((y << s) & b);
    y = y ^ ((y << t) & c);
    uint32_t z = y ^ (y >> l);

    return z; 
}
