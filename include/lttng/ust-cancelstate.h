/*
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * Copyright (C) 2021 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

#ifndef _LTTNG_UST_UST_CANCELSTATE_H
#define _LTTNG_UST_UST_CANCELSTATE_H

int lttng_ust_cancelstate_disable_push(void);
int lttng_ust_cancelstate_disable_pop(void);

#endif
