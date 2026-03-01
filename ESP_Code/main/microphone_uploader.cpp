#include "microphone_uploader.h"

#include <numeric>
#include <algorithm>
#include <array>
#include <deque>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "esp_log.h"
#include "network_rest.h"
#include "driver/adc.h"
#include "sdkconfig.h"

#define USE_READ_MIC_TASK 0

static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr size_t READ_CHUNK_SIZE = 2048;
static constexpr int MIC_SAMPLE_RATE_HZ = CONFIG_MIC_SAMPLE_RATE_HZ;
static constexpr int MIC_UPLOAD_WINDOW_SEC = CONFIG_MIC_UPLOAD_WINDOW_SEC;

#define SECONDS_TO_SAMPLES(seconds) (static_cast<size_t>(MIC_SAMPLE_RATE_HZ) * sizeof(int16_t) * static_cast<size_t>(seconds))

static constexpr size_t BYTES_PER_UPLOAD = SECONDS_TO_SAMPLES(MIC_UPLOAD_WINDOW_SEC);
static const char* TAG = "mic_uploader";

// Sound level sensor is used to determine when to start recording with the microphone, which uses I2S.
#define SOUND_LEVEL_SENSOR_GPIO GPIO_NUM_2

#if USE_READ_MIC_TASK
struct MicReadTaskContext {
	std::array<uint8_t, BYTES_PER_UPLOAD>* audio_buffer;
	size_t bytes_per_upload;
	std::atomic<size_t> total_bytes_read;
	std::atomic<bool> stop_requested;
	std::atomic<bool> finished;
	TaskHandle_t parent_task_handle;
	gpio_num_t blink_gpio;
};

static void microphone_read_task(void* pv_parameters) {
	MicReadTaskContext* context = static_cast<MicReadTaskContext*>(pv_parameters);
	if (context == nullptr || context->audio_buffer == nullptr) {
		vTaskDelete(nullptr);
		return;
	}

	if (context->parent_task_handle != nullptr) {
		while (ulTaskNotifyTake(pdTRUE, 0) > 0) {
		}
	}

	while (total_bytes_read < context->bytes_per_upload) {
		if (context->stop_requested.load(std::memory_order_relaxed) || ulTaskNotifyTake(pdTRUE, 0) > 0) {
			break;
		}

		size_t bytes_read = 0;
		const size_t bytes_to_read = std::min(READ_CHUNK_SIZE, context->bytes_per_upload - total_bytes_read);

		esp_err_t read_err = i2s_read(
			I2S_PORT,
			context->audio_buffer->data() + total_bytes_read,
			bytes_to_read,
			&bytes_read,
			pdMS_TO_TICKS(100)
		);

		if (read_err != ESP_OK) {
			ESP_LOGE(TAG, "i2s_read failed: %s", esp_err_to_name(read_err));
			continue;
		}

		if (bytes_read == 0) {
			ESP_LOGW(TAG, "i2s_read returned 0 bytes");
			continue;
		}

		total_bytes_read += bytes_read;
		context->total_bytes_read.store(total_bytes_read, std::memory_order_relaxed);
	}

	context->finished.store(true, std::memory_order_relaxed);

	if (context->parent_task_handle != nullptr) {
		xTaskNotifyGive(context->parent_task_handle);
	}

	vTaskDelete(nullptr);
}
#endif


static esp_err_t init_i2s_mic(const MicUploaderConfig* config) {
	if (config == nullptr) {
		return ESP_ERR_INVALID_ARG;
	}

	gpio_reset_pin(config->i2s_sel_gpio);
	gpio_set_direction(config->i2s_sel_gpio, GPIO_MODE_OUTPUT);
	gpio_set_level(config->i2s_sel_gpio, 0);

    gpio_reset_pin(SOUND_LEVEL_SENSOR_GPIO);
    gpio_set_direction(SOUND_LEVEL_SENSOR_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SOUND_LEVEL_SENSOR_GPIO, GPIO_FLOATING);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_11);

	i2s_config_t i2s_config = {};
	i2s_config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
	i2s_config.sample_rate = MIC_SAMPLE_RATE_HZ;
	i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
	i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
#ifdef I2S_COMM_FORMAT_STAND_I2S
	i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
	i2s_config.communication_format = I2S_COMM_FORMAT_I2S;
#endif
	i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
	i2s_config.dma_buf_count = 8;
	i2s_config.dma_buf_len = 512;
	i2s_config.use_apll = false;
	i2s_config.tx_desc_auto_clear = false;
	i2s_config.fixed_mclk = 0;

	i2s_pin_config_t pin_config = {};
	pin_config.bck_io_num = config->i2s_bclk_gpio;
	pin_config.ws_io_num = config->i2s_lrcl_gpio;
	pin_config.data_out_num = I2S_PIN_NO_CHANGE;
	pin_config.data_in_num = config->i2s_dout_gpio;

	esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2s_driver_install failed: %s", esp_err_to_name(err));
		return err;
	}

	err = i2s_set_pin(I2S_PORT, &pin_config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2s_set_pin failed: %s", esp_err_to_name(err));
		return err;
	}

	err = i2s_zero_dma_buffer(I2S_PORT);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2s_zero_dma_buffer failed: %s", esp_err_to_name(err));
		return err;
	}

	return ESP_OK;
}

void microphone_uploader_task(void* pv_parameters) {
	const MicUploaderConfig* config = static_cast<const MicUploaderConfig*>(pv_parameters);
	if (config == nullptr || config->endpoint == nullptr) {
		ESP_LOGE(TAG, "Invalid microphone uploader configuration");
		vTaskDelete(nullptr);
		return;
	}

	esp_err_t init_err = init_i2s_mic(config);
	if (init_err != ESP_OK) {
		ESP_LOGE(TAG, "I2S initialization failed");
		vTaskDelete(nullptr);
		return;
	}

	gpio_reset_pin(config->blink_gpio);
	gpio_set_direction(config->blink_gpio, GPIO_MODE_OUTPUT);

	static std::array<uint8_t, BYTES_PER_UPLOAD> audio_buffer = {};

	ESP_LOGI(TAG, "Microphone task started");

	while (true) {
		// Clear any pending notifications from the mic read task before starting a new capture session
		while (ulTaskNotifyTake(pdTRUE, 0) > 0) {
		}

		static constexpr float sound_threshold = 2000.0f;
		static constexpr float threshold_met_avg_threshold = 0.6f; // 60% of the recent samples must exceed the sound threshold
		
		// Wait for sound level to exceed threshold before starting to read from the microphone
		while (adc1_get_raw(ADC1_CHANNEL_2) < 2000) {
			vTaskDelay(pdMS_TO_TICKS(10));
		}
		ESP_LOGI(TAG, "Sound threshold exceeded, starting microphone read task");

		// Set LED to indicate we are listening
		size_t total_bytes_read = 0;
		gpio_set_level(config->blink_gpio, 1);
		
		#if USE_READ_MIC_TASK
		// Kick off the mic read task
		MicReadTaskContext read_task_context = {
			.audio_buffer = &audio_buffer,
			.bytes_per_upload = BYTES_PER_UPLOAD,
			.total_bytes_read = 0,
			.stop_requested = false,
			.finished = false,
			.parent_task_handle = xTaskGetCurrentTaskHandle(),
			.blink_gpio = config->blink_gpio,
		};
		
		TaskHandle_t mic_read_task_handle = nullptr;
		BaseType_t read_task_ok = xTaskCreate(
			microphone_read_task,
			"microphone_read_task",
			4096,
			&read_task_context,
			static_cast<UBaseType_t>(config->task_priority),
			&mic_read_task_handle
		);
		
		if (read_task_ok != pdPASS) {
			ESP_LOGE(TAG, "Failed to create microphone read task");
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}
		#endif
		
		// Monitor ADC to ensure that the sound level is consistently above the threshold before starting to read from the microphone, to avoid false triggers from brief noises. We can maintain a buffer of recent ADC readings and require that a certain percentage of them exceed the threshold before proceeding.
		static constexpr int sound_level_sampling_period_ms = 500;
		static constexpr size_t threshold_met_buf_size = 10000 / sound_level_sampling_period_ms ; // Buffer of 10000ms of audio level readings (assuming 500ms between readings)
		size_t threshold_samples_seen = 0;
		std::deque<float> threshold_met_buffer;
		static constexpr size_t threshold_met_grace_period = 5000 / sound_level_sampling_period_ms; // Allow up to 1000ms of readings to be below threshold when starting, to avoid cutting off the beginning of words
		
		while (
			true
		) {
			#if USE_READ_MIC_TASK
			if(read_task_context.finished.load(std::memory_order_relaxed)) {
				// Mic task has finished reading
				ESP_LOGI(TAG, "Microphone read task finished at max duration");
				break;
			}
			#endif

			
			if(
				threshold_met_buffer.size() > threshold_met_grace_period // Not enough samples collected yet to make a decision about sound level
				&& (std::accumulate(threshold_met_buffer.begin(), threshold_met_buffer.end(), 0.0f) / threshold_met_buffer.size()) < threshold_met_avg_threshold // Sound level is consistently below threshold
				) {
				ESP_LOGI(TAG, "Sound level dropped below threshold during capture, stopping read task");
				break;
			}

			// Check sound level and update buffer
			int adc_reading = adc1_get_raw(ADC1_CHANNEL_2);
			auto threshold_met = static_cast<float>(adc_reading < sound_threshold);
			threshold_met_buffer.push_back(threshold_met);
			if (threshold_met_buffer.size() > threshold_met_buf_size) {
				threshold_met_buffer.pop_front();
			}

			ESP_LOGI(TAG, "ADC reading: %d, threshold met: %d", adc_reading, static_cast<int>(threshold_met));

			vTaskDelay(pdMS_TO_TICKS(sound_level_sampling_period_ms));
		}
		ESP_LOGI(TAG, "Finished monitoring sound level, preparing to upload audio");

		// Turn off LED to indicate we are done listening
		gpio_set_level(config->blink_gpio, 0);

		#if USE_READ_MIC_TASK
		// Check if the mic read task is still running, and if so, signal it to stop and wait for it to finish before proceeding with the upload.
		if (!read_task_context.finished.load(std::memory_order_relaxed) && mic_read_task_handle != nullptr) {
			read_task_context.stop_requested.store(true, std::memory_order_relaxed);
			xTaskNotifyGive(mic_read_task_handle);
			ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
		}

		size_t total_bytes_read = read_task_context.total_bytes_read.load(std::memory_order_relaxed);

		size_t min_upload_length_bytes = SECONDS_TO_SAMPLES(3); // Minimum upload length of 3 seconds
		if (total_bytes_read < min_upload_length_bytes) {
			ESP_LOGW(TAG, "Captured audio is too short (%u bytes), skipping upload", static_cast<unsigned>(total_bytes_read));
			continue;
		}
		ESP_LOGI(TAG, "Captured %u bytes, uploading", static_cast<unsigned>(total_bytes_read));
		ESP_ERROR_CHECK_WITHOUT_ABORT(send_binary_post(config->endpoint, audio_buffer.data(), total_bytes_read));
		#else
		ESP_LOGI(TAG, "Finished recording audio");
		#endif

	}
}
