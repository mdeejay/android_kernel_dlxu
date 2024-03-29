/* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 *
 * HTC: elite machine driver which defines board-specific data
 * Copy from sound/soc/msm/msm8960.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/q6asm.h>
#include <mach/htc_acoustic_8960.h>
#include <linux/module.h>
#include "board-deluxe_u.h"
#include "../sound/soc/msm/msm-pcm-routing.h"

#include <linux/gpio.h>
#include <mach/tpa6185.h>
#include <mach/rt5501.h>
static atomic_t q6_effect_mode = ATOMIC_INIT(-1);
#define HAC_PAMP_GPIO	6
extern unsigned int system_rev;
extern unsigned int engineerid;

static int deluxeu_get_hw_component(void)
{
    int hw_com = 0;

    if(query_tpa6185())
        hw_com |= HTC_AUDIO_TPA6185;

    if(query_rt5501())
        hw_com |= HTC_AUDIO_RT5501;

    return hw_com;
}

static int deluxeu_enable_digital_mic(void)
{

    if(system_rev == XB && engineerid == 1)
        return 0;

    if(system_rev >= XB && engineerid == 0)
        return 1;

    return 0;
}

void apq8064_set_q6_effect_mode(int mode)
{
	pr_info("%s: mode %d\n", __func__, mode);
	atomic_set(&q6_effect_mode, mode);
}

int apq8064_get_q6_effect_mode(void)
{
	int mode = atomic_read(&q6_effect_mode);
	pr_info("%s: mode %d\n", __func__, mode);
	return mode;
}

static struct acoustic_ops acoustic = {
        .enable_digital_mic = deluxeu_enable_digital_mic,
        .get_hw_component = deluxeu_get_hw_component,
	.set_q6_effect = apq8064_set_q6_effect_mode,
};

static struct q6asm_ops qops = {
	.get_q6_effect = apq8064_get_q6_effect_mode,
};

static struct msm_pcm_routing_ops rops = {
	.get_q6_effect = apq8064_get_q6_effect_mode,
};

static int __init deluxe_u_audio_init(void)
{
        int ret = 0;

	static uint32_t audio_i2s_table[] = {
		GPIO_CFG(35, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		GPIO_CFG(36, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		GPIO_CFG(37, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
	};
	pr_info("%s", __func__);
	gpio_request(HAC_PAMP_GPIO, "AUDIO_HAC_AMP");
	gpio_direction_output(HAC_PAMP_GPIO, 0);
	gpio_free(HAC_PAMP_GPIO);
	gpio_tlmm_config(audio_i2s_table[0], GPIO_CFG_DISABLE);
	gpio_tlmm_config(audio_i2s_table[1], GPIO_CFG_DISABLE);
	gpio_tlmm_config(audio_i2s_table[2], GPIO_CFG_DISABLE);

	htc_register_q6asm_ops(&qops);
	htc_register_pcm_routing_ops(&rops);
	acoustic_register_ops(&acoustic);
	return ret;

}
late_initcall(deluxe_u_audio_init);

static void __exit deluxe_u_audio_exit(void)
{
	pr_info("%s", __func__);
}
module_exit(deluxe_u_audio_exit);

MODULE_DESCRIPTION("ALSA Platform Elite");
MODULE_LICENSE("GPL v2");
