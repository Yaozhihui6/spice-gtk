/*
 * GTK VNC Widget
 *
 * Copyright (C) 2006  Anthony Liguori <anthony@codemonkey.ws>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>

static GCond run_cond;
static GMutex run_lock;
static struct coroutine *current;
static struct coroutine leader;

#if 0
#define CO_DEBUG(OP) fprintf(stderr, "%s %p %s %d\n", OP, g_thread_self(), __FUNCTION__, __LINE__)
#else
#define CO_DEBUG(OP)
#endif

static void coroutine_system_init(void)
{
	if (current != NULL) {
		return;
	}

	CO_DEBUG("INIT");

	CO_DEBUG("LOCK");
	g_mutex_lock(&run_lock);

	/* The thread that creates the first coroutine is the system
	 * coroutine so let's fill out a structure for it */
	leader.entry = NULL;
	leader.stack_size = 0;
	leader.exited = 0;
	leader.thread = g_thread_self();
	leader.runnable = TRUE; /* we're the one running right now */
	leader.caller = NULL;
	leader.data = NULL;

	current = &leader;
}

static gpointer coroutine_thread(gpointer opaque)
{
	struct coroutine *co = opaque;
	CO_DEBUG("LOCK");
	g_mutex_lock(&run_lock);
	while (!co->runnable) {
		CO_DEBUG("WAIT");
		g_cond_wait(&run_cond, &run_lock);
	}

	CO_DEBUG("RUNNABLE");
	current = co;
	co->caller->data = co->entry(co->data);
	co->exited = 1;

	co->caller->runnable = TRUE;
	co->caller = NULL;
	CO_DEBUG("BROADCAST");
	g_cond_broadcast(&run_cond);
	CO_DEBUG("UNLOCK");
	g_mutex_unlock(&run_lock);

	return NULL;
}

void coroutine_init(struct coroutine *co)
{
	coroutine_system_init();
	CO_DEBUG("NEW");
	co->thread = g_thread_new("coroutine-thread",
				  coroutine_thread, co);
	co->exited = 0;
	co->runnable = FALSE;
	co->caller = NULL;
}

int coroutine_release(struct coroutine *co G_GNUC_UNUSED)
{
	return 0;
}

static void *coroutine_swap(struct coroutine *from, struct coroutine *to, void *arg)
{
	from->runnable = FALSE;
	to->runnable = TRUE;
	to->data = arg;
	CO_DEBUG("BROADCAST");
	g_cond_broadcast(&run_cond);
	CO_DEBUG("UNLOCK");
	g_mutex_unlock(&run_lock);
	CO_DEBUG("LOCK");
	g_mutex_lock(&run_lock);
	while (!from->runnable) {
	        CO_DEBUG("WAIT");
		g_cond_wait(&run_cond, &run_lock);
	}
	current = from;

	CO_DEBUG("SWAPPED");
	return from->data;
}

struct coroutine *coroutine_self(void)
{
	coroutine_system_init();
	return current;
}

void *coroutine_yieldto(struct coroutine *to, void *arg)
{
	g_return_val_if_fail(!to->caller, NULL);
	g_return_val_if_fail(!to->exited, NULL);

	CO_DEBUG("SWAP");
	to->caller = coroutine_self();
	return coroutine_swap(coroutine_self(), to, arg);
}

void *coroutine_yield(void *arg)
{
	struct coroutine *to = coroutine_self()->caller;
	if (!to) {
		fprintf(stderr, "Co-routine is yielding to no one\n");
		abort();
	}

	CO_DEBUG("SWAP");
	coroutine_self()->caller = NULL;
	return coroutine_swap(coroutine_self(), to, arg);
}

gboolean coroutine_is_main(struct coroutine *co)
{
    return (co == &leader);
}
