/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/devicetree/reconfigurable.h>
#include <zephyr/sys/iterable_sections.h>

struct dt_reconfigurable *dt_reconfigurable_get(int ord)
{
	STRUCT_SECTION_FOREACH(dt_reconfigurable, desc) {
		if (desc->ord == ord) {
			return desc;
		}
	}

	return NULL;
}

int dt_reconfigurable_patch(int ord, uintptr_t reg_addr, size_t reg_size, int32_t irq)
{
	struct dt_reconfigurable *desc = dt_reconfigurable_get(ord);

	if (desc == NULL) {
		return -ENOENT;
	}

	desc->reg_addr = reg_addr;
	desc->reg_size = reg_size;
	desc->irq = irq;

	return 0;
}

int dt_reconfigurable_set_present(int ord, bool present)
{
	struct dt_reconfigurable *desc = dt_reconfigurable_get(ord);

	if (desc == NULL) {
		return -ENOENT;
	}

	desc->present = present;

	return 0;
}

size_t dt_reconfigurable_count(void)
{
	size_t count;

	STRUCT_SECTION_COUNT(dt_reconfigurable, &count);

	return count;
}

int dt_reconfigurable_foreach(int (*cb)(struct dt_reconfigurable *desc, void *user), void *user)
{
	if (cb == NULL) {
		return -EINVAL;
	}

	STRUCT_SECTION_FOREACH(dt_reconfigurable, desc) {
		int ret = cb(desc, user);

		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

int dt_reconfigurable_apply(const struct dt_reconfigurable_manifest_entry *m, size_t n)
{
	int ret = 0;

	if ((m == NULL) && (n != 0U)) {
		return -EINVAL;
	}

	for (size_t i = 0; i < n; i++) {
		struct dt_reconfigurable *desc = dt_reconfigurable_get(m[i].ord);

		if (desc == NULL) {
			/* Keep applying the rest, but report that one was unknown. */
			ret = -ENOENT;
			continue;
		}

		desc->reg_addr = m[i].reg_addr;
		desc->reg_size = m[i].reg_size;
		desc->irq = m[i].irq;
		desc->present = m[i].present;
	}

	return ret;
}
