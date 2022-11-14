// Copyright 2020-2022 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"

/* App headers */
#include "app_conf.h"
#include "generic_pipeline.h"
#include "example_pipeline.h"
#include "platform/driver_instances.h"

#if ON_TILE(IO_TILE)

typedef struct {
    int32_t samples[appconfAUDIO_PIPELINE_CHANNELS][appconfAUDIO_PIPELINE_FRAME_ADVANCE];
    int32_t mic_samples_passthrough[appconfAUDIO_PIPELINE_CHANNELS][appconfAUDIO_PIPELINE_FRAME_ADVANCE];

    /* Below is additional context needed by other stages on a per frame basis */
    int32_t vnr_pred_flag;
    float_s32_t max_ref_energy;
    float_s32_t aec_corr_factor;
    int32_t ref_active_flag;
} frame_data_t;


#if appconfMIC_COUNT != 2
#error appconfMIC_COUNT must be 2
#endif

static BaseType_t xStage0_Gain = appconfAUDIO_PIPELINE_STAGE_ZERO_GAIN;

BaseType_t audiopipeline_get_stage1_gain( void )
{
    return xStage0_Gain;
}

BaseType_t audiopipeline_set_stage1_gain( BaseType_t xNewGain )
{
    xStage0_Gain = xNewGain;
    rtos_printf("Gain currently is: %d new gain is %d\n", xStage0_Gain, xNewGain);
    return xStage0_Gain;
}

static void *audio_pipeline_input_i(void *input_app_data)
{
    frame_data_t *frame_data;

    frame_data = pvPortMalloc(sizeof(frame_data_t));
    memset(frame_data, 0x00, sizeof(frame_data_t));

    size_t bytes_received = 0;
    bytes_received = rtos_intertile_rx_len(
            intertile_ctx,
            appconfAUDIOPIPELINE_PORT,
            portMAX_DELAY);

    xassert(bytes_received == sizeof(frame_data_t));

    rtos_intertile_rx_data(
            intertile_ctx,
            frame_data,
            bytes_received);

    return frame_data;
}

static int audio_pipeline_output_i(frame_data_t *frame_data,
                                   void *output_app_data)
{
    return audio_pipeline_output(output_app_data,
                               (int32_t **)frame_data->samples,
                               6,
                               appconfAUDIO_PIPELINE_FRAME_ADVANCE);
}
void stage1(int32_t * audio_frame)
{
    bfp_s32_t ch0, ch1;
    bfp_s32_init(&ch0, audio_frame, appconfEXP, appconfAUDIO_FRAME_LENGTH, 1);
    bfp_s32_init(&ch1, &audio_frame[appconfAUDIO_FRAME_LENGTH], appconfEXP, appconfAUDIO_FRAME_LENGTH, 1);
    // calculate the frame energy
    float_s32_t frame_energy_ch0 = float_s64_to_float_s32(bfp_s32_energy(&ch0));
    float_s32_t frame_energy_ch1 = float_s64_to_float_s32(bfp_s32_energy(&ch1));
    // calculate the frame power
    float frame_pow0 = float_s32_to_float(frame_energy_ch0) / (float)appconfAUDIO_FRAME_LENGTH;
    float frame_pow1 = float_s32_to_float(frame_energy_ch1) / (float)appconfAUDIO_FRAME_LENGTH;
    // get the led_port and mask out LED 0 and 1
    const rtos_gpio_port_id_t led_port = rtos_gpio_port(PORT_LEDS);
    uint32_t led_val = rtos_gpio_port_in(gpio_ctx_t0, led_port);
    led_val &= 0x3;
    // if the frame power exeedes the threshold turn on LED 2
    if((frame_pow0 > appconfPOWER_THRESHOLD) || (frame_pow1 > appconfPOWER_THRESHOLD)){
        led_val |= 0x4;
    }
    rtos_gpio_port_out(gpio_ctx_t0, led_port, led_val);
#if appconfPRINT_AUDIO_FRAME_POWER
    rtos_printf("Mic power:\nch0: %f\nch1: %f\n", frame_pow0, frame_pow1);
#endif
}

void stage0(int32_t * audio_frame)
{
    bfp_s32_t ch0, ch1;
    bfp_s32_init(&ch0, &audio_frame[0], appconfEXP, appconfAUDIO_FRAME_LENGTH, 1);
    bfp_s32_init(&ch1, &audio_frame[appconfAUDIO_FRAME_LENGTH], appconfEXP, appconfAUDIO_FRAME_LENGTH, 1);
    // convert dB to amplitude
    float power = (float)xStage0_Gain / 20.0;
    float gain_fl = powf(10.0, power);
    float_s32_t gain = float_to_float_s32(gain_fl);
    // scale both channels
    bfp_s32_scale(&ch0, &ch0, gain);
    bfp_s32_scale(&ch1, &ch1, gain);
    // normalise exponent
    bfp_s32_use_exponent(&ch0, appconfEXP);
    bfp_s32_use_exponent(&ch1, appconfEXP);
}

void example_pipeline_init(UBaseType_t priority)
{
	const int stage_count = 2;
    mic_array_ctx->format = RTOS_MIC_ARRAY_CHANNEL_SAMPLE;

	const pipeline_stage_t stages[stage_count] = {
			(pipeline_stage_t) stage0,
			(pipeline_stage_t) stage1
	};

	const configSTACK_DEPTH_TYPE stage_stack_sizes[stage_count] = {
			configMINIMAL_STACK_SIZE + RTOS_THREAD_STACK_SIZE(stage0) + RTOS_THREAD_STACK_SIZE(audio_pipeline_input_i),
			configMINIMAL_STACK_SIZE + RTOS_THREAD_STACK_SIZE(stage1) + RTOS_THREAD_STACK_SIZE(audio_pipeline_output_i)
	};

	generic_pipeline_init(
			audio_pipeline_input_i,
			audio_pipeline_output_i,
            NULL,
            NULL,
			stages,
			(const size_t*) stage_stack_sizes,
			priority,
			stage_count);
}

#undef MIN
#endif
