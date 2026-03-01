#pragma once

#include "driver/gpio.h"

struct MicUploaderConfig {
	const char* endpoint;
	gpio_num_t i2s_sel_gpio;
	gpio_num_t i2s_lrcl_gpio;
	gpio_num_t i2s_dout_gpio;
	gpio_num_t i2s_bclk_gpio;
	gpio_num_t blink_gpio;
	int task_stack_size;
	int task_priority;
};

void microphone_uploader_task(void* pv_parameters);
