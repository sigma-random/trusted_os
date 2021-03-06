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
#include <kern/thread.h>
#include <kern/thread_defs.h>
#include "thread_private.h"
#include <sm/teesmc.h>
#include <arm32.h>
#include <kern/mutex.h>
#include <kern/misc.h>
#include <kern/arch_debug.h>
#include <kprintf.h>

#include <assert.h>

static struct thread_ctx threads[NUM_THREADS];

static struct thread_core_local thread_core_local[NUM_CPUS];

thread_call_handler_t thread_stdcall_handler_ptr;
static thread_call_handler_t thread_fastcall_handler_ptr;
thread_fiq_handler_t thread_fiq_handler_ptr;
thread_svc_handler_t thread_svc_handler_ptr;
thread_abort_handler_t thread_abort_handler_ptr;

static struct mutex thread_global_lock = MUTEX_INITIALIZER;

static void lock_global(void)
{
	mutex_lock(&thread_global_lock);
}

static void unlock_global(void)
{
	mutex_unlock(&thread_global_lock);
}

static struct thread_core_local *get_core_local(void)
{
	struct thread_core_local *l;
	uint32_t cpu_id = get_core_pos();

	assert(cpu_id < NUM_CPUS);
	l = &thread_core_local[cpu_id];
	return l;
}

static bool have_one_active_thread(void)
{
	size_t n;

	for (n = 0; n < NUM_THREADS; n++) {
		if (threads[n].state == THREAD_STATE_ACTIVE)
			return true;
	}

	return false;
}

static void thread_alloc_and_run(struct thread_smc_args *args)
{
	size_t n;
	struct thread_core_local *l = get_core_local();
	bool found_thread = false;

	assert(l->curr_thread == -1);

	lock_global();

	if (!have_one_active_thread()) {
		for (n = 0; n < NUM_THREADS; n++) {
			if (threads[n].state == THREAD_STATE_FREE) {
				threads[n].state = THREAD_STATE_ACTIVE;
				found_thread = true;
				break;
			}
		}
	}

	unlock_global();

	if (!found_thread) {
		args->a0 = TEESMC_RETURN_EBUSY;
		args->a1 = 0;
		args->a2 = 0;
		args->a3 = 0;
		return;
	}

	l->curr_thread = n;

	threads[n].regs.pc = (uint32_t)thread_stdcall_entry;
	/* Stdcalls starts in SVC mode with unmasked IRQ or FIQ */
	threads[n].regs.cpsr = CPSR_MODE_SVC;
	/* Enable thumb mode if it's a thumb instruction */
	if (threads[n].regs.pc & 1)
		threads[n].regs.cpsr |= CPSR_T;
	/* Reinitialize stack pointer */
	threads[n].regs.svc_sp = threads[n].stack_va_end;

	/*
	 * Copy arguments into context. This will make the
	 * arguments appear in r0-r7 when thread is started.
	 */
	threads[n].regs.r0 = args->a0;
	threads[n].regs.r1 = args->a1;
	threads[n].regs.r2 = args->a2;
	threads[n].regs.r3 = args->a3;
	threads[n].regs.r4 = args->a4;
	threads[n].regs.r5 = args->a5;
	threads[n].regs.r6 = args->a6;
	threads[n].regs.r7 = args->a7;

	/* Save Hypervisor Client ID */
	threads[n].hyp_clnt_id = args->a7;

	thread_resume(&threads[n].regs);
}

static void thread_resume_from_rpc(struct thread_smc_args *args)
{
	size_t n = args->a3; /* thread id */
	struct thread_core_local *l = get_core_local();
	uint32_t rv = 0;

	assert(l->curr_thread == -1);

	lock_global();

	if (have_one_active_thread()) {
		rv = TEESMC_RETURN_EBUSY;
	} else if (n < NUM_THREADS &&
		threads[n].state == THREAD_STATE_SUSPENDED &&
		args->a7 == threads[n].hyp_clnt_id) {
		threads[n].state = THREAD_STATE_ACTIVE;
	} else {
		rv = TEESMC_RETURN_ERESUME;
	}

	unlock_global();

	if (rv) {
		args->a0 = rv;
		args->a1 = 0;
		args->a2 = 0;
		args->a3 = 0;
		return;
	}

	l->curr_thread = n;

	/*
	 * Return from RPC to request service of an IRQ must not
	 * get parameters from non-secure world.
	 */
	if (threads[n].flags & THREAD_FLAGS_COPY_ARGS_ON_RETURN) {
		/*
		 * Update returned values from RPC, values will appear in
		 * r0-r3 when thread is resumed.
		 */
		threads[n].regs.r0 = args->a0;
		threads[n].regs.r1 = args->a1;
		threads[n].regs.r2 = args->a2;
		threads[n].regs.r3 = args->a3;
		threads[n].flags &= ~THREAD_FLAGS_COPY_ARGS_ON_RETURN;
	}

	thread_resume(&threads[n].regs);
}

void thread_handle_smc_call(struct thread_smc_args *args)
{
	check_canaries();

	if (TEESMC_IS_FAST_CALL(args->a0)) {
		thread_fastcall_handler_ptr(args);
	} else {
		if (args->a0 == TEESMC32_CALL_RETURN_FROM_RPC)
			thread_resume_from_rpc(args);
		else
			thread_alloc_and_run(args);
	}
}

void *thread_get_tmp_sp(void)
{
	struct thread_core_local *l = get_core_local();

	return (void *)l->tmp_stack_va_end;
}

void thread_state_free(void)
{
	struct thread_core_local *l = get_core_local();

	assert(l->curr_thread != -1);

	lock_global();

	assert(threads[l->curr_thread].state == THREAD_STATE_ACTIVE);
	threads[l->curr_thread].state = THREAD_STATE_FREE;
	threads[l->curr_thread].flags = 0;
	l->curr_thread = -1;

	unlock_global();
}

int thread_state_suspend(uint32_t flags, uint32_t cpsr, uint32_t pc)
{
	struct thread_core_local *l = get_core_local();
	int ct = l->curr_thread;

	assert(ct != -1);

	check_canaries();

	lock_global();

	assert(threads[ct].state == THREAD_STATE_ACTIVE);
	threads[ct].flags &= ~THREAD_FLAGS_COPY_ARGS_ON_RETURN;
	threads[ct].flags |= flags & THREAD_FLAGS_COPY_ARGS_ON_RETURN;
	threads[ct].regs.cpsr = cpsr;
	threads[ct].regs.pc = pc;
	threads[ct].state = THREAD_STATE_SUSPENDED;
	l->curr_thread = -1;

	unlock_global();

	return ct;
}


bool thread_init_stack(uint32_t thread_id, vaddr_t sp)
{
	switch (thread_id) {
	case THREAD_TMP_STACK:
		{
			struct thread_core_local *l = get_core_local();

			l->tmp_stack_va_end = sp;
			l->curr_thread = -1;

			thread_set_irq_sp(sp);
			thread_set_fiq_sp(sp);
		}
		break;

	case THREAD_ABT_STACK:
		thread_set_abt_sp(sp);
		break;

	default:
		if (thread_id >= NUM_THREADS)
			return false;
		if (threads[thread_id].state != THREAD_STATE_FREE)
			return false;

		threads[thread_id].stack_va_end = sp;
	}

	return true;
}

void thread_init_handlers(const struct thread_handlers *handlers)
{
	thread_stdcall_handler_ptr = handlers->stdcall;
	thread_fastcall_handler_ptr = handlers->fastcall;
	thread_fiq_handler_ptr = handlers->fiq;
	thread_svc_handler_ptr = handlers->svc;
	thread_abort_handler_ptr = handlers->abort;
	thread_init_vbar();
}

void thread_set_tsd(void *tsd, thread_tsd_free_t free_func)
{
	struct thread_core_local *l = get_core_local();

	assert(l->curr_thread != -1);
	assert(threads[l->curr_thread].state == THREAD_STATE_ACTIVE);
	threads[l->curr_thread].tsd = tsd;
	threads[l->curr_thread].tsd_free = free_func;
}

void *thread_get_tsd(void)
{
	struct thread_core_local *l = get_core_local();
	int ct = l->curr_thread;

	if (ct == -1 || threads[ct].state != THREAD_STATE_ACTIVE)
		return NULL;
	else
		return threads[ct].tsd;
}

struct thread_ctx_regs *thread_get_ctx_regs(void)
{
	struct thread_core_local *l = get_core_local();

	assert(l->curr_thread != -1);
	return &threads[l->curr_thread].regs;
}

void thread_rpc_alloc(size_t arg_size, size_t payload_size, paddr_t *arg,
		paddr_t *payload)
{
	uint32_t rpc_args[THREAD_RPC_NUM_ARGS] = {
		TEESMC_RETURN_RPC_ALLOC, arg_size, payload_size};

	thread_rpc(rpc_args);
	if (arg)
		*arg = rpc_args[1];
	if (payload)
		*payload = rpc_args[2];
}

void thread_rpc_free(paddr_t arg, paddr_t payload)
{
	uint32_t rpc_args[THREAD_RPC_NUM_ARGS] = {
		TEESMC_RETURN_RPC_FREE, arg, payload};

	thread_rpc(rpc_args);
}

void thread_rpc_cmd(paddr_t arg)
{
	uint32_t rpc_args[THREAD_RPC_NUM_ARGS] = {TEESMC_RETURN_RPC_CMD, arg};

	thread_rpc(rpc_args);
}
