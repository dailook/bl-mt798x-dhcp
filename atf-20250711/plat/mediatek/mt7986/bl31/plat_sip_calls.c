/*
 * Copyright (c) 2023, MediaTek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <common/runtime_svc.h>
#include <mtk_sip_svc.h>
#include <apsoc_sip_svc_common.h>
#include <rng.h>

/* MT7986 legacy TRNG (v1-style) register layout fallback. */
#if MT7986_TRNG_VERSION == 2

#include <common/debug.h>
#include <drivers/delay_timer.h>
#include <lib/mmio.h>
#include <errno.h>

/* Compatibility FID for callers using SMC32 convention. */
#define MTK_SIP_TRNG_GET_RND_COMPAT32		0x82000550U

#define MT7986_TRNG_V1_CTRL			(TRNG_BASE + 0x0000)
#define MT7986_TRNG_V1_DATA			(TRNG_BASE + 0x0008)
#define MT7986_TRNG_V1_EN			BIT(0)
#define MT7986_TRNG_V1_READY			BIT(31)

static uintptr_t mt7986_trng_v1_fallback_get_rnd(uint32_t *value)
{
	uint64_t time;

	if (value == NULL)
		return (uintptr_t)-EINVAL;

	mmio_setbits_32(MT7986_TRNG_V1_CTRL, MT7986_TRNG_V1_EN);
	time = timeout_init_us(MTK_TIMEOUT_POLL);

	while (!(mmio_read_32(MT7986_TRNG_V1_CTRL) & MT7986_TRNG_V1_READY)) {
		if (timeout_elapsed(time))
			return (uintptr_t)-EAGAIN;
	}

	*value = mmio_read_32(MT7986_TRNG_V1_DATA);

	return 0;
}
#endif

static uintptr_t mt7986_sip_trng_get_rnd(uint32_t smc_fid, u_register_t x1,
					 u_register_t x2, u_register_t x3,
					 u_register_t x4, void *cookie,
					 void *handle, u_register_t flags)
{
	uint32_t value = 0;
	uintptr_t ret;

	ret = plat_get_rnd(&value);

#if MT7986_TRNG_VERSION == 2
	NOTICE("TRNG: using v2 TRNG to get random value: 0x%08x (SMC FID: 0x%08x)\n", value, smc_fid);
	if (ret) {
		uintptr_t fb_ret;

		fb_ret = mt7986_trng_v1_fallback_get_rnd(&value);
		if (!fb_ret) {
			ret = 0;
		} else {
			WARN("mt7986 trng get rnd failed: 0x%lx (fallback: 0x%lx)\n",
			     ret, fb_ret);
			ret = fb_ret;
		}
	}
#endif

	SMC_RET2(handle, ret, value);
}

struct mtk_sip_call_record mtk_plat_sip_calls[] = {
	MTK_SIP_CALL_RECORD(MTK_SIP_TRNG_GET_RND, mt7986_sip_trng_get_rnd),
	// MTK_SIP_CALL_RECORD(MTK_SIP_TRNG_GET_RND_COMPAT32, mt7986_sip_trng_get_rnd),
};

struct mtk_sip_call_record mtk_plat_sip_calls_from_sec[] = {
	MTK_SIP_CALL_RECORD(MTK_SIP_TRNG_GET_RND, mt7986_sip_trng_get_rnd),
	// MTK_SIP_CALL_RECORD(MTK_SIP_TRNG_GET_RND_COMPAT32, mt7986_sip_trng_get_rnd),
};

const uint32_t mtk_plat_sip_call_num = ARRAY_SIZE(mtk_plat_sip_calls);
const uint32_t mtk_plat_sip_call_num_from_sec = ARRAY_SIZE(mtk_plat_sip_calls_from_sec);
