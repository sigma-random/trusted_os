/*
 * Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sm/sm.h>

#include <arm32.h>

#include <plat.h>

/*
 * sm_smc_entry uses 6 * 4 bytes
 * sm_fiq_entry uses 12 * 4 bytes
 */
#define SM_STACK_SIZE	(12 * 4)

static struct sm_nsec_ctx sm_nsec_ctx[NUM_CPUS];
static struct sm_sec_ctx sm_sec_ctx[NUM_CPUS];
static struct sm_pre_fiq_ctx sm_pre_fiq_ctx[NUM_CPUS];
static uint32_t sm_sps[NUM_CPUS][SM_STACK_SIZE / 4]
	__attribute__((section(".bss.prebss.stack"), aligned(STACK_ALIGMENT)));

#define CPU_ID()	(read_mpidr() & MPIDR_CPU_MASK)

struct sm_nsec_ctx *sm_get_nsec_ctx(void)
{
	return &sm_nsec_ctx[CPU_ID()];
}

struct sm_sec_ctx *sm_get_sec_ctx(void)
{
	return &sm_sec_ctx[CPU_ID()];
}

struct sm_pre_fiq_ctx *sm_get_pre_fiq_ctx(void)
{
	return &sm_pre_fiq_ctx[CPU_ID()];
}

void *sm_get_sp(void)
{
	return &sm_sps[CPU_ID()][SM_STACK_SIZE / 4];
}