#include "microphone_uploader.h"

#include <numeric>
#include <algorithm>
#include <array>
#include <deque>
#include <limits>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "esp_log.h"
#include "network_rest.h"
#include "driver/adc.h"
#include "sdkconfig.h"

static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr size_t READ_CHUNK_SIZE = 2048;
static constexpr int MIC_SAMPLE_RATE_HZ = CONFIG_MIC_SAMPLE_RATE_HZ;
static constexpr int I2S_SELECT_LEVEL = 0;
static constexpr size_t PCM_BYTES_PER_SAMPLE = sizeof(int16_t);
static constexpr size_t I2S_READ_BYTES_PER_SAMPLE = sizeof(int32_t);
static constexpr size_t I2S_READ_CHUNK_BYTES = READ_CHUNK_SIZE * 2;

#define MILLISECONDS_TO_BYTES_PCM16(milliseconds) ((static_cast<size_t>(MIC_SAMPLE_RATE_HZ) * PCM_BYTES_PER_SAMPLE * static_cast<size_t>(milliseconds) / 1000))

static constexpr size_t BYTES_PER_UPLOAD = MILLISECONDS_TO_BYTES_PCM16(CONFIG_MIC_UPLOAD_WINDOW_MS);
static const char* TAG = "mic_uploader";

// Sound level sensor is used to determine when to start recording with the microphone, which uses I2S.
#define SOUND_LEVEL_SENSOR_GPIO GPIO_NUM_2


static esp_err_t init_i2s_mic(const MicUploaderConfig* config) {
	if (config == nullptr) {
		return ESP_ERR_INVALID_ARG;
	}

	gpio_reset_pin(config->i2s_sel_gpio);
	gpio_set_direction(config->i2s_sel_gpio, GPIO_MODE_OUTPUT);
	gpio_set_level(config->i2s_sel_gpio, I2S_SELECT_LEVEL);

    gpio_reset_pin(SOUND_LEVEL_SENSOR_GPIO);
    gpio_set_direction(SOUND_LEVEL_SENSOR_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SOUND_LEVEL_SENSOR_GPIO, GPIO_FLOATING);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_11);

	i2s_config_t i2s_config = {};
	i2s_config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
	i2s_config.sample_rate = MIC_SAMPLE_RATE_HZ;
	i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
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

static int16_t convert_i2s_32_to_pcm16(int32_t raw_sample) {
	const int32_t shifted = raw_sample >> 16;
	if (shifted > std::numeric_limits<int16_t>::max()) {
		return std::numeric_limits<int16_t>::max();
	}
	if (shifted < std::numeric_limits<int16_t>::min()) {
		return std::numeric_limits<int16_t>::min();
	}
	return static_cast<int16_t>(shifted);
}

// static int16_t clamp_to_pcm16(int32_t value) {
// 	if (value > std::numeric_limits<int16_t>::max()) {
// 		return std::numeric_limits<int16_t>::max();
// 	}
// 	if (value < std::numeric_limits<int16_t>::min()) {
// 		return std::numeric_limits<int16_t>::min();
// 	}
// 	return static_cast<int16_t>(value);
// }

// static int16_t remove_dc_offset(int16_t* samples, size_t sample_count) {
// 	if (samples == nullptr || sample_count == 0) {
// 		return 0;
// 	}

// 	int64_t sum = 0;
// 	for (size_t index = 0; index < sample_count; ++index) {
// 		sum += samples[index];
// 	}

// 	const int32_t mean = static_cast<int32_t>(sum / static_cast<int64_t>(sample_count));
// 	for (size_t index = 0; index < sample_count; ++index) {
// 		samples[index] = clamp_to_pcm16(static_cast<int32_t>(samples[index]) - mean);
// 	}

// 	return static_cast<int16_t>(mean);
// }

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

	static std::array<int16_t, BYTES_PER_UPLOAD / sizeof(int16_t)> audio_buffer = {};
	std::array<int32_t, I2S_READ_CHUNK_BYTES / sizeof(int32_t)> i2s_read_buffer = {};

	ESP_LOGI(TAG, "Microphone task started");
	ESP_LOGI(
		TAG,
		"I2S pin map BCLK=%d WS=%d DIN=%d L/R_SEL=%d level=%d",
		config->i2s_bclk_gpio,
		config->i2s_lrcl_gpio,
		config->i2s_dout_gpio,
		config->i2s_sel_gpio,
		I2S_SELECT_LEVEL
	);

	while (true) {
		gpio_set_level(config->blink_gpio, 1);
		size_t total_samples_captured = 0;
		int16_t min_sample = std::numeric_limits<int16_t>::max();
		int16_t max_sample = std::numeric_limits<int16_t>::min();
		size_t non_zero_samples = 0;
		std::array<int16_t, 8> first_samples = {};
		size_t first_samples_filled = 0;

		while (total_samples_captured < audio_buffer.size()) {
			size_t bytes_read = 0;
			const size_t samples_remaining = audio_buffer.size() - total_samples_captured;
			const size_t bytes_to_read = std::min(I2S_READ_CHUNK_BYTES, samples_remaining * I2S_READ_BYTES_PER_SAMPLE);

			esp_err_t read_err = i2s_read(
				I2S_PORT,
				i2s_read_buffer.data(),
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

			const size_t samples_read = bytes_read / I2S_READ_BYTES_PER_SAMPLE;
			if (samples_read == 0) {
				ESP_LOGW(TAG, "i2s_read returned %u bytes (< one sample)", static_cast<unsigned>(bytes_read));
				continue;
			}

			for (size_t sample_index = 0; sample_index < samples_read && total_samples_captured < audio_buffer.size(); ++sample_index) {
				const int16_t pcm16_sample = convert_i2s_32_to_pcm16(i2s_read_buffer[sample_index]);
				audio_buffer[total_samples_captured] = pcm16_sample;
				if (pcm16_sample < min_sample) {
					min_sample = pcm16_sample;
				}
				if (pcm16_sample > max_sample) {
					max_sample = pcm16_sample;
				}
				if (pcm16_sample != 0) {
					++non_zero_samples;
				}
				if (first_samples_filled < first_samples.size()) {
					first_samples[first_samples_filled] = pcm16_sample;
					++first_samples_filled;
				}
				++total_samples_captured;
			}
		}

		ESP_LOGI(TAG, "Finished monitoring sound level, preparing to upload audio");

		// Turn off LED to indicate we are done listening
		gpio_set_level(config->blink_gpio, 0);

		size_t total_bytes_read = total_samples_captured * PCM_BYTES_PER_SAMPLE;
		size_t min_upload_length_bytes = MILLISECONDS_TO_BYTES_PCM16(0); // Minimum upload length of 3 seconds
		if (total_bytes_read < min_upload_length_bytes) {
			ESP_LOGW(TAG, "Captured audio is too short (%u bytes), skipping upload", static_cast<unsigned>(total_bytes_read));
			continue;
		}

		// const int16_t removed_dc = remove_dc_offset(audio_buffer.data(), total_samples_captured);
		const int16_t removed_dc = 0;
		ESP_LOGI(
			TAG,
			"Captured %u bytes (%u samples), non-zero samples=%u, min=%d, max=%d, dc=%d, first=[%d,%d,%d,%d,%d,%d,%d,%d]",
			static_cast<unsigned>(total_bytes_read),
			static_cast<unsigned>(total_samples_captured),
			static_cast<unsigned>(non_zero_samples),
			min_sample,
			max_sample,
			removed_dc,
			first_samples[0],
			first_samples[1],
			first_samples[2],
			first_samples[3],
			first_samples[4],
			first_samples[5],
			first_samples[6],
			first_samples[7]
		);
		ESP_LOGI(TAG, "Uploading audio payload");
		ESP_ERROR_CHECK_WITHOUT_ABORT(send_binary_post(config->endpoint, reinterpret_cast<const uint8_t*>(audio_buffer.data()), total_bytes_read));

	}
}
