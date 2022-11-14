// Copyright 2020-2021 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#pragma once


void inference_task_init( UBaseType_t priority );
void fill_input_tensor( uint32_t* data_to_send, int nbytes );
void empty_output_tensor( uint32_t* buffer_to_fill, int nbytes );
void invoke();



