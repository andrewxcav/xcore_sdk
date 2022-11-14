// Copyright 2019-2022 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

/* System headers */
#include <platform.h>
#include <xs1.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "queue.h"
#include "rtos_intertile.h"

/* App headers */
#include "app_conf.h"
#include "platform/platform_init.h"
#include "platform/driver_instances.h"
#include "mem_analysis/mem_analysis.h"
#include "example_pipeline/example_pipeline.h"
#include "gpio_ctrl/gpio_ctrl.h"
#include "uart/uart_demo.h"
#include "inference/inference_task.h"



#define AUDIO_PIPELINE_DONT_FREE_FRAME 0
#define AUDIO_PIPELINE_FREE_FRAME      1

void audio_pipeline_init(
        void *input_app_data,
        void *output_app_data);

void audio_pipeline_input(
        void *input_app_data,
        int32_t **input_audio_frames,
        size_t ch_count,
        size_t frame_count);

int audio_pipeline_output(
        void *output_app_data,
        int32_t **output_audio_frames,
        size_t ch_count,
        size_t frame_count);


void audio_pipeline_input(void *input_app_data,
                        int32_t **input_audio_frames,
                        size_t ch_count,
                        size_t frame_count)
{
    (void) input_app_data;
    int32_t **mic_ptr = (int32_t **)(input_audio_frames + (2 * frame_count));

    static int flushed;
    while (!flushed) {
        size_t received;
        received = rtos_mic_array_rx(mic_array_ctx,
                                     mic_ptr,
                                     frame_count,
                                     0);
        if (received == 0) {
            rtos_mic_array_rx(mic_array_ctx,
                              mic_ptr,
                              frame_count,
                              portMAX_DELAY);
            flushed = 1;
        }
    }

    /*
     * NOTE: ALWAYS receive the next frame from the PDM mics,
     * even if USB is the current mic source. The controls the
     * timing since usb_audio_recv() does not block and will
     * receive all zeros if no frame is available yet.
     */
    rtos_mic_array_rx(mic_array_ctx,
                      mic_ptr,
                      frame_count,
                      portMAX_DELAY);

#if appconfUSB_ENABLED
    int32_t **usb_mic_audio_frame = NULL;
    size_t ch_cnt = 2;  /* ref frames */

    if (aec_ref_source == appconfAEC_REF_USB) {
        usb_mic_audio_frame = input_audio_frames;
    }

    if (mic_from_usb) {
        ch_count += 2;  /* mic frames */
    }

    /*
     * As noted above, this does not block.
     * and expects ref L, ref R, mic 0, mic 1
     */
    usb_audio_recv(intertile_ctx,
                   frame_count,
                   usb_mic_audio_frame,
                   ch_count);
#endif

#if appconfI2S_ENABLED
    if (!appconfUSB_ENABLED || aec_ref_source == appconfAEC_REF_I2S) {
        /* This shouldn't need to block given it shares a clock with the PDM mics */

        xassert(frame_count == appconfAUDIO_PIPELINE_FRAME_ADVANCE);
        /* I2S provides sample channel format */
        int32_t tmp[appconfAUDIO_PIPELINE_FRAME_ADVANCE][appconfAUDIO_PIPELINE_CHANNELS];
        int32_t *tmpptr = (int32_t *)input_audio_frames;

        size_t rx_count =
        rtos_i2s_rx(i2s_ctx,
                    (int32_t*) tmp,
                    frame_count,
                    portMAX_DELAY);
        xassert(rx_count == frame_count);

        for (int i=0; i<frame_count; i++) {
            /* ref is first */
            *(tmpptr + i) = tmp[i][0];
            *(tmpptr + i + frame_count) = tmp[i][1];
        }
    }
#endif
}

int audio_pipeline_output(void *output_app_data,
                        int32_t **output_audio_frames,
                        size_t ch_count,
                        size_t frame_count)
{
    (void) output_app_data;

#if appconfI2S_ENABLED
#if appconfI2S_MODE == appconfI2S_MODE_MASTER
#if !appconfI2S_TDM_ENABLED
    xassert(frame_count == appconfAUDIO_PIPELINE_FRAME_ADVANCE);
    /* I2S expects sample channel format */
    int32_t tmp[appconfAUDIO_PIPELINE_FRAME_ADVANCE][appconfAUDIO_PIPELINE_CHANNELS];
    int32_t *tmpptr = (int32_t *)output_audio_frames;
    for (int j=0; j<frame_count; j++) {
        /* ASR output is first */
        tmp[j][0] = *(tmpptr+j+(2*frame_count));    // ref 0
        tmp[j][1] = *(tmpptr+j+(3*frame_count));    // ref 1
    }

    rtos_i2s_tx(i2s_ctx,
                (int32_t*) tmp,
                frame_count,
                portMAX_DELAY);
#else
    int32_t *tmpptr = (int32_t *)output_audio_frames;
    for (int i = 0; i < frame_count; i++) {
        /* output_audio_frames format is
         *   processed_audio_frame
         *   reference_audio_frame
         *   raw_mic_audio_frame
         */
        int32_t tdm_output[6];

        tdm_output[0] = *(tmpptr + i + (4 * frame_count)) & ~0x1;   // mic 0
        tdm_output[1] = *(tmpptr + i + (5 * frame_count)) & ~0x1;   // mic 1
        tdm_output[2] = *(tmpptr + i + (2 * frame_count)) & ~0x1;   // ref 0
        tdm_output[3] = *(tmpptr + i + (3 * frame_count)) & ~0x1;   // ref 1
        tdm_output[4] = *(tmpptr + i) | 0x1;                        // proc 0
        tdm_output[5] = *(tmpptr + i + frame_count) | 0x1;          // proc 1

        rtos_i2s_tx(i2s_ctx,
                    tdm_output,
                    appconfI2S_AUDIO_SAMPLE_RATE / appconfAUDIO_PIPELINE_SAMPLE_RATE,
                    portMAX_DELAY);
    }
#endif
#elif appconfI2S_MODE == appconfI2S_MODE_SLAVE
    /* I2S expects sample channel format */
    int32_t tmp[appconfAUDIO_PIPELINE_FRAME_ADVANCE][appconfAUDIO_PIPELINE_CHANNELS];
    int32_t *tmpptr = (int32_t *)output_audio_frames;
    for (int j=0; j<appconfAUDIO_PIPELINE_FRAME_ADVANCE; j++) {
        /* ASR output is first */
        tmp[j][0] = *(tmpptr+j);
        tmp[j][1] = *(tmpptr+j+appconfAUDIO_PIPELINE_FRAME_ADVANCE);
    }

    rtos_intertile_tx(intertile_ctx,
                      appconfI2S_OUTPUT_SLAVE_PORT,
                      tmp,
                      sizeof(tmp));
#endif
#endif

#if appconfUSB_ENABLED
    usb_audio_send(intertile_ctx,
                frame_count,
                output_audio_frames,
                6);
#endif

#if appconfWW_ENABLED
    ww_audio_send(intertile_ctx,
                  frame_count,
                  (int32_t(*)[2])output_audio_frames);
#endif

    return AUDIO_PIPELINE_FREE_FRAME;
}

void vApplicationMallocFailedHook( void )
{
    rtos_printf("Malloc Failed on tile %d!\n", THIS_XCORE_TILE);
    for(;;);
}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
    rtos_printf("\nStack Overflow!!! %d %s!\n", THIS_XCORE_TILE, pcTaskName);
    configASSERT(0);
}

void startup_task(void *arg)
{
    rtos_printf("Startup task running from tile %d on core %d\n", THIS_XCORE_TILE, portGET_CORE_ID());

    platform_start();

// Two pipelines that span two tiles, adapted from: https://github.com/xmos/sln_voice/blob/develop/examples/stlp/audio_pipeline/src/adec/audio_pipeline_t0.c

#if ON_TILE(INFERENCE_TILE)
    /* Init neural network  */
    inference_task_init(appconfINFERENCE_TASK_PRIORITY); // task doing inference 
 
#endif

#if ON_TILE(IO_TILE)
    /* Create the gpio control task */
    gpio_ctrl_create(appconfGPIO_TASK_PRIORITY);

    /* Create audio pipeline */
    example_pipeline_init(appconfAUDIO_PIPELINE_TASK_PRIORITY);// task marshalling/pre-processing audio
    

#endif

	for (;;) {
		rtos_printf("Tile[%d]:\n\tMinimum heap free: %d\n\tCurrent heap free: %d\n", THIS_XCORE_TILE, xPortGetMinimumEverFreeHeapSize(), xPortGetFreeHeapSize());
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}

static void tile_common_init(chanend_t c)
{
    platform_init(c);
    chanend_free(c);

    xTaskCreate((TaskFunction_t) startup_task,
                "startup_task",
                RTOS_THREAD_STACK_SIZE(startup_task),
                NULL,
                appconfSTARTUP_TASK_PRIORITY,
                NULL);

    rtos_printf("start scheduler on tile %d\n", THIS_XCORE_TILE);
    vTaskStartScheduler();
}

#if ON_TILE(0)
void main_tile0(chanend_t c0, chanend_t c1, chanend_t c2, chanend_t c3) {
    (void)c0;
    (void)c2;
    (void)c3;

    tile_common_init(c1);
}
#endif

#if ON_TILE(1)
void main_tile1(chanend_t c0, chanend_t c1, chanend_t c2, chanend_t c3) {
    (void)c1;
    (void)c2;
    (void)c3;

    tile_common_init(c0);
}
#endif
