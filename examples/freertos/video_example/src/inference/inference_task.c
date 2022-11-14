// Copyright 2020-2022 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"

/* App headers */
#include "app_conf.h"
//#include "generic_pipeline.h"
#include "inference_task.h"
#include "platform/driver_instances.h"


void fill_input_tensor( uint32_t* data_to_send, int nbytes )
{

}

void empty_output_tensor( uint32_t* buffer_to_fill, int nbytes )
{

}


void invoke()
{

}

void inference_task_init(UBaseType_t priority)
{
	const configSTACK_DEPTH_TYPE stage_stack_sizes[1] = {
			configMINIMAL_STACK_SIZE + RTOS_THREAD_STACK_SIZE(invoke) + RTOS_THREAD_STACK_SIZE(fill_input_tensor) + RTOS_THREAD_STACK_SIZE(empty_output_tensor)
	};


}

#undef MIN
