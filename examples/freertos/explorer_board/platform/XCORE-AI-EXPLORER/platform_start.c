// Copyright (c) 2021 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

#include <platform.h>

#include "app_conf.h"
#include "platform/driver_instances.h"
#include "aic3204.h"

static void gpio_start(void)
{
    rtos_gpio_rpc_config(gpio_ctx_t0, appconfGPIO_T0_RPC_PORT, appconfGPIO_RPC_HOST_PRIORITY);
    rtos_gpio_rpc_config(gpio_ctx_t1, appconfGPIO_T1_RPC_PORT, appconfGPIO_RPC_HOST_PRIORITY);

#if ON_TILE(0)
    rtos_gpio_start(gpio_ctx_t0);
#endif
#if ON_TILE(1)
    rtos_gpio_start(gpio_ctx_t1);
#endif
}

static void spi_start(void)
{
#if ON_TILE(0)
    rtos_spi_master_start(spi_master_ctx, appconfSPI_MASTER_TASK_PRIORITY);
#endif
}

static void i2c_master_start(void)
{
    rtos_i2c_master_rpc_config(i2c_master_ctx, appconfI2C_MASTER_RPC_PORT, appconfI2C_MASTER_RPC_PRIORITY);
#if ON_TILE(0)
    rtos_i2c_master_start(i2c_master_ctx);
#endif
}

static void flash_start(void)
{
#if ON_TILE(0)
    rtos_qspi_flash_start(qspi_flash_ctx, appconfQSPI_FLASH_TASK_PRIORITY);
#endif
}

static void audio_codec_start(void)
{
#if ON_TILE(0)
    if (aic3204_init() != 0) {
        rtos_printf("DAC initialization failed\n");
    }
#endif
}

static void mics_start(void)
{
#if ON_TILE(1)
    const int pdm_decimation_factor = rtos_mic_array_decimation_factor(
            appconfPDM_CLOCK_FREQUENCY,
            appconfPIPELINE_AUDIO_SAMPLE_RATE);

    rtos_mic_array_start(
            mic_array_ctx,
            pdm_decimation_factor,
            rtos_mic_array_third_stage_coefs(pdm_decimation_factor),
            rtos_mic_array_fir_compensation(pdm_decimation_factor),
            2 * MIC_DUAL_FRAME_SIZE,
            appconfPDM_MIC_INTERRUPT_CORE);
#endif
}

static void i2s_start(void)
{
#if ON_TILE(1)
    rtos_i2s_start(
            i2s_ctx,
            rtos_i2s_mclk_bclk_ratio(appconfAUDIO_CLOCK_FREQUENCY, appconfPIPELINE_AUDIO_SAMPLE_RATE),
            I2S_MODE_I2S,
            2.2 * appconfAUDIO_FRAME_LENGTH,
            1.2 * appconfAUDIO_FRAME_LENGTH,
            appconfI2S_INTERRUPT_CORE);
#endif
}

void platform_start(void)
{
    rtos_intertile_start(intertile_ctx);

    gpio_start();
    spi_start();
    flash_start();
    i2c_master_start();
    audio_codec_start();
    mics_start();
    i2s_start();
}
