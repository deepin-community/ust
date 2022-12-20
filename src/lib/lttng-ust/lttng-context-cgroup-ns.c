/*
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * Copyright (C) 2009-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 * Copyright (C) 2019 Michael Jeanson <mjeanson@efficios.com>
 *
 * LTTng UST cgroup namespace context.
 */

#define _LGPL_SOURCE
#include <limits.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <lttng/ust-events.h>
#include <lttng/ust-tracer.h>
#include "common/compat/tid.h"
#include <urcu/tls-compat.h>
#include <lttng/ust-ringbuffer-context.h>

#include "context-internal.h"
#include "lttng-tracer-core.h"
#include "common/ns.h"


/*
 * We cache the result to ensure we don't stat(2) the proc filesystem on
 * each event.
 */
static DEFINE_URCU_TLS_INIT(ino_t, cached_cgroup_ns, NS_INO_UNINITIALIZED);

static
ino_t get_cgroup_ns(void)
{
	struct stat sb;
	ino_t cgroup_ns;

	cgroup_ns = CMM_LOAD_SHARED(URCU_TLS(cached_cgroup_ns));

	/*
	 * If the cache is populated, do nothing and return the
	 * cached inode number.
	 */
	if (caa_likely(cgroup_ns != NS_INO_UNINITIALIZED))
		return cgroup_ns;

	/*
	 * At this point we have to populate the cache, set the initial
	 * value to NS_INO_UNAVAILABLE (0), if we fail to get the inode
	 * number from the proc filesystem, this is the value we will
	 * cache.
	 */
	cgroup_ns = NS_INO_UNAVAILABLE;

	/*
	 * /proc/thread-self was introduced in kernel v3.17
	 */
	if (stat("/proc/thread-self/ns/cgroup", &sb) == 0) {
		cgroup_ns = sb.st_ino;
	} else {
		char proc_ns_path[LTTNG_PROC_NS_PATH_MAX];

		if (snprintf(proc_ns_path, LTTNG_PROC_NS_PATH_MAX,
				"/proc/self/task/%d/ns/cgroup",
				lttng_gettid()) >= 0) {

			if (stat(proc_ns_path, &sb) == 0) {
				cgroup_ns = sb.st_ino;
			}
		}
	}

	/*
	 * And finally, store the inode number in the cache.
	 */
	CMM_STORE_SHARED(URCU_TLS(cached_cgroup_ns), cgroup_ns);

	return cgroup_ns;
}

/*
 * The cgroup namespace can change for 3 reasons
 *  * clone(2) called with CLONE_NEWCGROUP
 *  * setns(2) called with the fd of a different cgroup ns
 *  * unshare(2) called with CLONE_NEWCGROUP
 */
void lttng_context_cgroup_ns_reset(void)
{
	CMM_STORE_SHARED(URCU_TLS(cached_cgroup_ns), NS_INO_UNINITIALIZED);
}

static
size_t cgroup_ns_get_size(void *priv __attribute__((unused)),
		struct lttng_ust_probe_ctx *probe_ctx __attribute__((unused)),
		size_t offset)
{
	size_t size = 0;

	size += lttng_ust_ring_buffer_align(offset, lttng_ust_rb_alignof(ino_t));
	size += sizeof(ino_t);
	return size;
}

static
void cgroup_ns_record(void *priv __attribute__((unused)),
		struct lttng_ust_probe_ctx *probe_ctx __attribute__((unused)),
		struct lttng_ust_ring_buffer_ctx *ctx,
		struct lttng_ust_channel_buffer *chan)
{
	ino_t cgroup_ns;

	cgroup_ns = get_cgroup_ns();
	chan->ops->event_write(ctx, &cgroup_ns, sizeof(cgroup_ns),
			lttng_ust_rb_alignof(cgroup_ns));
}

static
void cgroup_ns_get_value(void *priv __attribute__((unused)),
		struct lttng_ust_probe_ctx *probe_ctx __attribute__((unused)),
		struct lttng_ust_ctx_value *value)
{
	value->u.u64 = get_cgroup_ns();
}

static const struct lttng_ust_ctx_field *ctx_field = lttng_ust_static_ctx_field(
	lttng_ust_static_event_field("cgroup_ns",
		lttng_ust_static_type_integer(sizeof(ino_t) * CHAR_BIT,
				lttng_ust_rb_alignof(ino_t) * CHAR_BIT,
				lttng_ust_is_signed_type(ino_t),
				LTTNG_UST_BYTE_ORDER, 10),
		false, false),
	cgroup_ns_get_size,
	cgroup_ns_record,
	cgroup_ns_get_value,
	NULL, NULL);

int lttng_add_cgroup_ns_to_ctx(struct lttng_ust_ctx **ctx)
{
	int ret;

	if (lttng_find_context(*ctx, ctx_field->event_field->name)) {
		ret = -EEXIST;
		goto error_find_context;
	}
	ret = lttng_ust_context_append(ctx, ctx_field);
	if (ret)
		return ret;
	return 0;

error_find_context:
	return ret;
}

/*
 * Force a read (imply TLS allocation for dlopen) of TLS variables.
 */
void lttng_cgroup_ns_alloc_tls(void)
{
	asm volatile ("" : : "m" (URCU_TLS(cached_cgroup_ns)));
}
