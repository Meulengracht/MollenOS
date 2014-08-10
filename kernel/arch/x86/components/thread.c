/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation ? , either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS X86-32 Threads
*/

/* Includes */
#include <arch.h>
#include <assert.h>
#include <scheduler.h>
#include <thread.h>
#include <memory.h>
#include <list.h>
#include <lapic.h>
#include <heap.h>
#include <gdt.h>
#include <string.h>
#include <stdio.h>

/* Globals */
list_t *threads = NULL;
list_t *zombies = NULL;
volatile tid_t glb_thread_id = 0;
volatile list_node_t *glb_current_threads[64];
volatile list_node_t *glb_idle_threads[64];
volatile uint8_t glb_threading_enabled = 0;
spinlock_t glb_thread_lock;

/* Externs */
extern volatile uint32_t timer_quantum;
extern uint32_t memory_get_cr3(void);
extern void save_fpu(addr_t *buffer);
extern void set_ts(void);
extern void _yield(void);
extern void enter_thread(registers_t *regs);

/* The YIELD handler */
int threading_yield(void *args)
{
	/* Get registers */
	registers_t *regs = NULL;
	uint32_t time_slice = 20;
	uint32_t task_priority = 0;
	cpu_t cpu = get_cpu();

	/* Send EOI */
	apic_send_eoi();

	/* Switch Task */ 
	regs = (void*)threading_switch((registers_t*)args, 0, &time_slice, &task_priority);

	/* If we just got hold of idle task, well fuck it disable timer
	* untill we get another task */
	if (!(threading_get_current_thread(cpu)->flags & X86_THREAD_IDLE))
	{
		/* Set Task Priority */
		apic_set_task_priority(61 - task_priority);

		/* Reset Timer Tick */
		apic_write_local(LAPIC_INITIAL_COUNT, timer_quantum * time_slice);

		/* Re-enable timer in one-shot mode */
		apic_write_local(LAPIC_TIMER_VECTOR, INTERRUPT_TIMER);		//0x20000 - Periodic
	}
	else
	{
		apic_write_local(LAPIC_TIMER_VECTOR, 0x10000);
		apic_set_task_priority(0);
	}

	/* Enter new thread */
	enter_thread(regs);

	/* Never reached */
	return X86_IRQ_HANDLED;
}

/* Initialization 
 * Creates the main thread */
void threading_init(void)
{
	thread_t *init;
	list_node_t *node;

	/* Create threading list */
	threads = list_create(LIST_SAFE);
	zombies = list_create(LIST_SAFE);
	glb_thread_id = 0;

	/* Setup initial thread */
	init = (thread_t*)kmalloc(sizeof(thread_t));
	init->name = strdup("Idle");
	init->fpu_buffer = kmalloc_a(0x1000);
	init->priority = 60;
	init->flags = X86_THREAD_FPU_INITIALISED | X86_THREAD_USEDFPU | X86_THREAD_CPU_BOUND | X86_THREAD_IDLE;
	init->time_slice = MCORE_IDLE_TIMESLICE;
	init->parent_id = 0xDEADBEEF;
	init->thread_id = glb_thread_id;
	init->cpu_id = 0;
	init->context = NULL;
	init->user_context = NULL;
	init->cr3 = memory_get_cr3();
	init->page_dir = memory_get_current_pdir(0);
	init->func = NULL;
	init->args = NULL;
	init->sleep_resource = NULL;

	/* Reset lock */
	spinlock_reset(&glb_thread_lock);

	/* Memset the buffer */
	memset(init->fpu_buffer, 0, 0x1000);

	/* Create a node for the scheduler */
	node = list_create_node(glb_thread_id, init);
	glb_current_threads[0] = node;
	glb_idle_threads[0] = node;


	/* Create a node for the thread-list */
	node = list_create_node(glb_thread_id, init);
	list_append(threads, node);

	/* Increase Id */
	glb_thread_id++;

	/* Install Yield */
	interrupt_install_soft(INTERRUPT_YIELD, threading_yield, NULL);

	/* Enable */
	glb_threading_enabled = 1;
}

/* Initialises AP task */
void threading_ap_init(void)
{
	cpu_t cpu;
	thread_t *init;
	list_node_t *node;

	/* Acquire Lock */
	spinlock_acquire_nint(&glb_thread_lock);

	/* Setup initial thread */
	cpu = get_cpu();
	init = (thread_t*)kmalloc(sizeof(thread_t));
	init->name = strdup("ApIdle");
	init->fpu_buffer = kmalloc_a(0x1000);
	init->priority = 60;
	init->flags = X86_THREAD_FPU_INITIALISED | X86_THREAD_USEDFPU | X86_THREAD_CPU_BOUND | X86_THREAD_IDLE;
	init->time_slice = MCORE_IDLE_TIMESLICE;
	init->parent_id = 0xDEADBEEF;
	init->thread_id = glb_thread_id;
	init->cpu_id = cpu;
	init->context = NULL;
	init->user_context = NULL;
	init->cr3 = memory_get_cr3();
	init->page_dir = memory_get_current_pdir(cpu);
	init->func = NULL;
	init->args = NULL;
	init->sleep_resource = NULL;

	/* Memset the buffer */
	memset(init->fpu_buffer, 0, 0x1000);

	/* Create a node for the scheduler */
	node = list_create_node(glb_thread_id, init);
	glb_current_threads[cpu] = node;
	glb_idle_threads[cpu] = node;

	/* Create a node for the thread list */
	node = list_create_node(glb_thread_id, init);
	list_append(threads, node);

	/* Increase Id */
	glb_thread_id++;

	/* Release */
	spinlock_release_nint(&glb_thread_lock);
}

/* Get Current Thread */
thread_t *threading_get_current_thread(cpu_t cpu)
{
	/* Get thread */
	return (thread_t*)glb_current_threads[cpu]->data;
}

/* Get Current Scheduler(!!!) Node */
list_node_t *threading_get_current_node(cpu_t cpu)
{
	/* Get thread */
	return (list_node_t*)glb_current_threads[cpu];
}

tid_t threading_get_thread_id(void)
{
	cpu_t cpu = get_cpu();

	if (glb_thread_id == 0)
		return 0;
	else
		return threading_get_current_thread(cpu)->thread_id;
}

/* Marks current thread for sleep */
void *threading_enter_sleep(void)
{
	cpu_t cpu = get_cpu();
	thread_t *t = threading_get_current_thread(cpu);
	t->flags |= X86_THREAD_ENTER_SLEEP;
	return threading_get_current_node(cpu);
}

/* Set Current List Node */
void threading_set_current_node(cpu_t cpu, list_node_t *node)
{
	glb_current_threads[cpu] = node;
}

/* Cleanup a thread */
void threading_cleanup_thread(thread_t *thread)
{
	_CRT_UNUSED(thread);
}

/* Prints threads */
void threading_debug(void)
{
	foreach(i, threads)
	{
		thread_t *t = i->data;
		printf("Thread %u (%s) - Flags %u, Priority %u, Timeslice %u\n",
			t->thread_id, t->name, t->flags, t->priority, t->time_slice);
	}
}

/* This is actually every thread entry point, 
 * It makes sure to handle ALL threads terminating */
void threading_start(void)
{
	thread_t *t;
	cpu_t cpu;

	/* Get cpu */
	cpu = get_cpu();

	/* Get current thread */
	t = threading_get_current_thread(cpu);

	/* Call entry point */
	t->func(t->args);

	/* IF WE REACH THIS POINT THREAD IS DONE! */
	t->flags |= X86_THREAD_FINISHED;

	/* Yield */
	_yield();

	/* Safety-Catch */
	for (;;);
}

/* Create a new thread */
tid_t threading_create_thread(char *name, thread_entry function, void *args, int flags)
{
	thread_t *t, *parent;
	cpu_t cpu;
	list_node_t *node;

	/* Get spinlock */
	spinlock_acquire(&glb_thread_lock);

	/* Get cpu */
	cpu = get_cpu();
	parent = threading_get_current_thread(cpu);

	/* Allocate a new thread structure */
	t = (thread_t*)kmalloc(sizeof(thread_t));

	/* Setup */
	t->name = strdup(name);
	t->func = function;
	t->args = args;
	t->context = context_create((addr_t)threading_start);
	t->user_context = NULL;

	/* If we are CPU bound :/ */
	if (flags & THREADING_CPUBOUND)
	{
		t->flags = X86_THREAD_CPU_BOUND;
		t->cpu_id = cpu;
	}
	else
	{
		/* Select the low bearing CPU */
		t->flags = 0;
		t->cpu_id = 0xFF;
	}

	t->parent_id = parent->thread_id;
	t->thread_id = glb_thread_id;
	t->sleep_resource = NULL;

	/* Scheduler Related */
	t->priority = -1;
	t->time_slice = MCORE_INITIAL_TIMESLICE;
	
	/* Memory */
	t->cr3 = memory_get_cr3();
	t->page_dir = memory_get_current_pdir(cpu);

	/* FPU */
	t->fpu_buffer = (addr_t*)kmalloc_a(0x1000);

	/* Memset the buffer */
	memset(t->fpu_buffer, 0, 0x1000);

	/* Increase id */
	glb_thread_id++;

	/* Release lock */
	spinlock_release(&glb_thread_lock);

	/* Append it to list & scheduler */
	node = list_create_node(t->thread_id, t);
	list_append(threads, node);

	/* Ready */
	node = list_create_node(t->thread_id, t);
	scheduler_ready_thread(node);

	return t->thread_id;
}

/* Task Switch occurs here */
registers_t *threading_switch(registers_t *regs, int preemptive, uint32_t *time_slice, uint32_t *task_priority)
{
	cpu_t cpu;
	thread_t *t;
	list_node_t *node;

	/* Sanity */
	if (glb_threading_enabled == 0)
		return regs;

	/* Get CPU */
	cpu = get_cpu();

	/* Get thread */
	t = threading_get_current_thread(cpu);

	/* What the fuck?? */
	assert(t != NULL && regs != NULL);

	/* Save FPU/MMX/SSE State */
	if (t->flags & X86_THREAD_USEDFPU)
		save_fpu(t->fpu_buffer);

	/* Save stack */
	if (t->flags & X86_THREAD_USERMODE)
		t->user_context = regs;
	else
		t->context = regs;

	/* Get a new task! */
	node = threading_get_current_node(cpu);

	/* Unless this one is done.. */
	if (t->flags & X86_THREAD_FINISHED || t->flags & X86_THREAD_IDLE
		|| t->flags & X86_THREAD_ENTER_SLEEP)
	{
		/* Someone should really kill those zombies :/ */
		if (t->flags & X86_THREAD_FINISHED)
		{
			list_node_t *n = list_get_data_by_id(threads, t->thread_id, 0);
			list_remove_by_node(threads, n);
			list_append(zombies, n);
		}

		/* Remove flag so it does not happen again */
		if (t->flags & X86_THREAD_ENTER_SLEEP)
			t->flags &= ~(X86_THREAD_ENTER_SLEEP);

		node = scheduler_schedule(cpu, NULL, preemptive);
	}
	else
	{
		/* Yea we dont schedule idle tasks :-) */
		node = scheduler_schedule(cpu, node, preemptive);
	}

	/* Sanity */
	if (node == NULL)
		node = (list_node_t*)glb_idle_threads[cpu];

	/* Update current */
	threading_set_current_node(cpu, node);
	t = threading_get_current_thread(cpu);

	/* Update user variables */
	*time_slice = t->time_slice;
	*task_priority = t->priority;

	/* Update Addressing Space */
	memory_switch_directory(cpu, (page_directory_t*)t->page_dir, t->cr3);

	/* Set TSS */
	gdt_update_tss(cpu, (addr_t)t->context);

	/* Finish Transition */
	if (t->flags & X86_THREAD_TRANSITION)
	{
		t->flags &= ~X86_THREAD_TRANSITION;
		t->flags |= X86_THREAD_USERMODE;
	}

	/* Clear FPU/MMX/SSE */
	t->flags &= ~X86_THREAD_USEDFPU;

	/* Set TS bit in CR0 */
	set_ts();

	/* Return new stack */
	if (t->flags & X86_THREAD_USERMODE)
		return t->user_context;
	else
		return t->context;
}