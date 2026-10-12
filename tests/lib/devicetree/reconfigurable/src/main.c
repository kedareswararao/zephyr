/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/devicetree/reconfigurable.h>

#define RECFG0 DT_NODELABEL(recfg0)
#define RECFG1 DT_NODELABEL(recfg1)

#define RECFG0_ORD DT_DEP_ORD(RECFG0)
#define RECFG1_ORD DT_DEP_ORD(RECFG1)

/* Build the mutable descriptor table from the two devicetree nodes. */
DT_RECONFIGURABLE_DEFINE(RECFG0);
DT_RECONFIGURABLE_DEFINE(RECFG1);

ZTEST(dt_reconfigurable, test_count)
{
	zassert_equal(dt_reconfigurable_count(), 2, "expected 2 descriptors");
}

ZTEST(dt_reconfigurable, test_dt_seeding)
{
	struct dt_reconfigurable *r0 = dt_reconfigurable_get(RECFG0_ORD);
	struct dt_reconfigurable *r1 = dt_reconfigurable_get(RECFG1_ORD);

	zassert_not_null(r0, "recfg0 descriptor missing");
	zassert_not_null(r1, "recfg1 descriptor missing");

	/* Seeded from devicetree reg. */
	zassert_equal(r0->reg_addr, 0x10000000, "recfg0 base not seeded from DT");
	zassert_equal(r0->reg_size, 0x1000, "recfg0 size not seeded from DT");
	zassert_equal(r1->reg_addr, 0x20000000, "recfg1 base not seeded from DT");
	zassert_equal(r1->reg_size, 0x2000, "recfg1 size not seeded from DT");

	/* No interrupts in DT -> irq seeds to -1. */
	zassert_equal(r0->irq, -1, "recfg0 irq should seed to -1");

	/* present seeded from DT status. */
	zassert_true(r0->present, "recfg0 (okay) should be present");
	zassert_false(r1->present, "recfg1 (disabled) should not be present");
}

ZTEST(dt_reconfigurable, test_unknown_ord)
{
	zassert_is_null(dt_reconfigurable_get(0x7fffffff), "unknown ord returned a descriptor");
	zassert_equal(dt_reconfigurable_patch(0x7fffffff, 0, 0, 0), -ENOENT,
		      "patch of unknown ord should return -ENOENT");
	zassert_equal(dt_reconfigurable_set_present(0x7fffffff, true), -ENOENT,
		      "set_present of unknown ord should return -ENOENT");
}

ZTEST(dt_reconfigurable, test_patch)
{
	struct dt_reconfigurable *r0 = dt_reconfigurable_get(RECFG0_ORD);

	zassert_ok(dt_reconfigurable_patch(RECFG0_ORD, 0x40000000, 0x8000, 42),
		   "patch failed");

	zassert_equal(r0->reg_addr, 0x40000000, "patched base not reflected");
	zassert_equal(r0->reg_size, 0x8000, "patched size not reflected");
	zassert_equal(r0->irq, 42, "patched irq not reflected");

	/* Restore so other tests are order-independent. */
	zassert_ok(dt_reconfigurable_patch(RECFG0_ORD, 0x10000000, 0x1000, -1), "restore failed");
}

ZTEST(dt_reconfigurable, test_set_present)
{
	struct dt_reconfigurable *r1 = dt_reconfigurable_get(RECFG1_ORD);

	zassert_ok(dt_reconfigurable_set_present(RECFG1_ORD, true), "set_present failed");
	zassert_true(r1->present, "present not updated");

	zassert_ok(dt_reconfigurable_set_present(RECFG1_ORD, false), "restore failed");
}

static int count_cb(struct dt_reconfigurable *desc, void *user)
{
	size_t *n = user;

	ARG_UNUSED(desc);
	(*n)++;

	return 0;
}

static int stop_cb(struct dt_reconfigurable *desc, void *user)
{
	ARG_UNUSED(desc);
	ARG_UNUSED(user);

	return 7;
}

ZTEST(dt_reconfigurable, test_foreach)
{
	size_t n = 0;

	zassert_ok(dt_reconfigurable_foreach(count_cb, &n), "foreach failed");
	zassert_equal(n, 2, "foreach did not visit all descriptors");

	/* Early stop propagates the callback's return value. */
	zassert_equal(dt_reconfigurable_foreach(stop_cb, NULL), 7, "early stop not propagated");

	zassert_equal(dt_reconfigurable_foreach(NULL, NULL), -EINVAL, "NULL cb not rejected");
}

ZTEST(dt_reconfigurable, test_apply_manifest)
{
	struct dt_reconfigurable *r0 = dt_reconfigurable_get(RECFG0_ORD);
	struct dt_reconfigurable *r1 = dt_reconfigurable_get(RECFG1_ORD);
	const struct dt_reconfigurable_manifest_entry manifest[] = {
		{ .ord = RECFG0_ORD, .reg_addr = 0xa000, .reg_size = 0x100,
		  .irq = 11, .present = true },
		{ .ord = RECFG1_ORD, .reg_addr = 0xb000, .reg_size = 0x200,
		  .irq = 12, .present = true },
	};

	zassert_ok(dt_reconfigurable_apply(manifest, ARRAY_SIZE(manifest)), "apply failed");

	zassert_equal(r0->reg_addr, 0xa000, "r0 base not applied");
	zassert_equal(r0->irq, 11, "r0 irq not applied");
	zassert_true(r0->present, "r0 present not applied");
	zassert_equal(r1->reg_addr, 0xb000, "r1 base not applied");
	zassert_true(r1->present, "r1 present not applied");

	/* Unknown ordinal in the manifest: known entries still applied, -ENOENT returned. */
	const struct dt_reconfigurable_manifest_entry bad[] = {
		{ .ord = RECFG0_ORD, .reg_addr = 0x10000000, .reg_size = 0x1000,
		  .irq = -1, .present = true },
		{ .ord = 0x7fffffff, .reg_addr = 0, .reg_size = 0, .irq = 0, .present = false },
	};

	zassert_equal(dt_reconfigurable_apply(bad, ARRAY_SIZE(bad)), -ENOENT,
		      "unknown manifest ord should return -ENOENT");
	zassert_equal(r0->reg_addr, 0x10000000, "valid entry before unknown not applied");

	zassert_equal(dt_reconfigurable_apply(NULL, 1), -EINVAL, "NULL manifest not rejected");

	/* Restore the devicetree baseline so other tests are order-independent. */
	zassert_ok(dt_reconfigurable_patch(RECFG0_ORD, 0x10000000, 0x1000, -1),
		   "r0 restore failed");
	zassert_ok(dt_reconfigurable_patch(RECFG1_ORD, 0x20000000, 0x2000, -1),
		   "r1 restore failed");
	zassert_ok(dt_reconfigurable_set_present(RECFG0_ORD, true), "r0 present restore failed");
	zassert_ok(dt_reconfigurable_set_present(RECFG1_ORD, false), "r1 present restore failed");
}

ZTEST_SUITE(dt_reconfigurable, NULL, NULL, NULL, NULL, NULL);
