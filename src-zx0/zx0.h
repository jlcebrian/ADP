#ifndef ADP_ZX0_H
#define ADP_ZX0_H

#include <os_lib.h>

#ifdef __cplusplus
#define ZX0_NULL 0
#else
#define ZX0_NULL NULL
#endif

/*
 * (c) Copyright 2021 by Einar Saukas. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The name of its author may not be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define INITIAL_OFFSET 1

#define FALSE 0
#define TRUE 1

typedef struct block_t {
    struct block_t *chain;
    struct block_t *ghost_chain;
    int bits;
    int index;
    int offset;
    int references;
} BLOCK;

static inline void* zx0_alloc(size_t size)
{
    return OSAlloc(size);
}

static inline void zx0_free(void* ptr)
{
    if (ptr != 0)
        OSFree(ptr);
}

static inline void* zx0_calloc(size_t count, size_t size)
{
    void* ptr = OSAlloc(count * size);
    if (ptr != 0)
        MemClear(ptr, count * size);
    return ptr;
}

static inline void zx0_fail(const char* message)
{
#ifdef _DEBUGPRINT
    DebugPrintf("%s\n", message);
#endif
    Abort();
}

BLOCK *allocate(int bits, int index, int offset, BLOCK *chain);

void assign(BLOCK **ptr, BLOCK *chain);

BLOCK *optimize(unsigned char *input_data, int input_size, int skip, int offset_limit);

unsigned char *compress(BLOCK *optimal, unsigned char *input_data, int input_size, int skip, int backwards_mode, int invert_mode, int *output_size, int *delta);

#endif
