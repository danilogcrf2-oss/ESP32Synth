#pragma once
#include "ESP32Synth.h"
#include "soc/ledc_reg.h"
#include "soc/interrupts.h" // Fixes the ETS_LEDC_INTR_SOURCE error
#include "esp_intr_alloc.h"

// Fixes the SYNTH_PWM_MODE error
#if defined(CONFIG_IDF_TARGET_ESP32)
    #define SYNTH_PWM_MODE LEDC_HIGH_SPEED_MODE
#else
    #define SYNTH_PWM_MODE LEDC_LOW_SPEED_MODE
#endif

// Auto-Synchronized ISR: Triggered by the hardware PWM's own heartbeat pulse
static IRAM_ATTR void ledc_ovf_isr(void *user_ctx) {
    ESP32Synth* synth = (ESP32Synth*)user_ctx;
    uint32_t int_st = REG_READ(LEDC_INT_ST_REG);
    bool handled = false;

    #if defined(CONFIG_IDF_TARGET_ESP32)
    // Classic ESP32 - High Speed Channel 0 Timer 0
    if (int_st & LEDC_HSTIMER0_OVF_INT_ST) {
        REG_WRITE(LEDC_INT_CLR_REG, LEDC_HSTIMER0_OVF_INT_CLR); // MUST clear the flag
        handled = true;

        if (synth->_running && synth->pwm_ping_pong_buf[synth->pwm_active_buf]) {
            int16_t sample = synth->pwm_ping_pong_buf[synth->pwm_active_buf][synth->pwm_read_idx];
            synth->pwm_read_idx = synth->pwm_read_idx + 1; // Avoids 'volatile ++' compiler warning
            uint32_t duty_val = ((uint32_t)(sample + 32768) >> 6) << 4; // Exact 10-bit duty
            REG_WRITE(LEDC_HSCH0_DUTY_REG, duty_val);
            REG_WRITE(LEDC_HSCH0_CONF1_REG, REG_READ(LEDC_HSCH0_CONF1_REG) | (1U << 31)); // Immediate latch
        }
    }
    #else
    // ESP32-S3 - Low Speed Channel 0 Timer 0
    if (int_st & LEDC_LSTIMER0_OVF_INT_ST) {
        REG_WRITE(LEDC_INT_CLR_REG, LEDC_LSTIMER0_OVF_INT_CLR); // MUST clear the flag
        handled = true;

        if (synth->_running && synth->pwm_ping_pong_buf[synth->pwm_active_buf]) {
            int16_t sample = synth->pwm_ping_pong_buf[synth->pwm_active_buf][synth->pwm_read_idx];
            synth->pwm_read_idx = synth->pwm_read_idx + 1; // Avoids 'volatile ++' compiler warning
            uint32_t duty_val = ((uint32_t)(sample + 32768) >> 6) << 4; // Exact 10-bit duty
            REG_WRITE(LEDC_LSCH0_DUTY_REG, duty_val);
            REG_WRITE(LEDC_LSCH0_CONF0_REG, REG_READ(LEDC_LSCH0_CONF0_REG) | 0x10); // Immediate latch via BIT 4
        }
    }
    #endif

    // If the buffer is exhausted, request more from the Render Task!
    if (handled && synth->_running && synth->pwm_read_idx >= synth->pwm_block_samples) {
        synth->pwm_read_idx = 0;
        synth->pwm_active_buf = 1 - synth->pwm_active_buf;
        BaseType_t awoken = pdFALSE;
        xSemaphoreGiveFromISR(synth->pwm_sema, &awoken);
        if (awoken == pdTRUE) portYIELD_FROM_ISR(); // Wake the processor immediately
    }
}

void ESP32Synth::end() {
    this->_running = false;

    for (int i = 0; i < MAX_VOICES; i++) {
        voices[i].active       = false;
        voices[i].envState     = ENV_IDLE;
        voices[i].streamTrackId = -1;
    }

    while (audioTaskHandle  != NULL) { vTaskDelay(pdMS_TO_TICKS(2)); }
    while (streamTaskHandle != NULL) { vTaskDelay(pdMS_TO_TICKS(2)); }

    for (int i = 0; i < MAX_STREAMS; i++) {
        if (streams[i].active) {
            streams[i].playing = false;
            streams[i].active  = false;
            SYNTH_FILE_CLOSE(streams[i].file);
        }
    }

    if (tx_handle != NULL) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
    }
    #if defined(CONFIG_IDF_TARGET_ESP32)
    if (dac_handle != NULL) {
        dac_continuous_disable(dac_handle);
        dac_continuous_del_channels(dac_handle);
        dac_handle = NULL;
    }
    #endif

    if (pwm_sema != NULL) {
        if (pwm_timer) {
            // Disable the interrupt directly in hardware before freeing memory
            #if defined(CONFIG_IDF_TARGET_ESP32)
                REG_WRITE(LEDC_INT_ENA_REG, REG_READ(LEDC_INT_ENA_REG) & ~LEDC_HSTIMER0_OVF_INT_ENA);
            #else
                REG_WRITE(LEDC_INT_ENA_REG, REG_READ(LEDC_INT_ENA_REG) & ~LEDC_LSTIMER0_OVF_INT_ENA);
            #endif
            esp_intr_free((intr_handle_t)pwm_timer);
            pwm_timer = NULL;
        }
        vSemaphoreDelete(pwm_sema);
        pwm_sema = NULL;
    }
    if (pwm_ping_pong_buf[0]) { heap_caps_free(pwm_ping_pong_buf[0]); pwm_ping_pong_buf[0] = nullptr; }
    if (pwm_ping_pong_buf[1]) { heap_caps_free(pwm_ping_pong_buf[1]); pwm_ping_pong_buf[1] = nullptr; }

    if (_dataPin >= 0) gpio_reset_pin((gpio_num_t)_dataPin);
    if (_bckPin  >= 0) gpio_reset_pin((gpio_num_t)_bckPin);
    if (_wsPin   >= 0) gpio_reset_pin((gpio_num_t)_wsPin);
    if (_mclkPin >= 0) gpio_reset_pin((gpio_num_t)_mclkPin);

    _dataPin = -1; _bckPin = -1; _wsPin = -1; _mclkPin = -1;
}

bool ESP32Synth::begin(int dacPin) {
    return begin(dacPin, SMODE_DAC, -1, -1, -1, I2S_16BIT);
}

bool ESP32Synth::begin(int bckPin, int wsPin, int dataPin) {
    return begin(dataPin, SMODE_I2S, bckPin, wsPin, -1, I2S_16BIT);
}

bool ESP32Synth::begin(int bckPin, int wsPin, int dataPin, I2S_Depth i2sDepth) {
    return begin(dataPin, SMODE_I2S, bckPin, wsPin, -1, i2sDepth);
}

bool ESP32Synth::begin(int bckPin, int wsPin, int dataPin, int mclkPin, I2S_Depth i2sDepth) {
    return begin(dataPin, SMODE_I2S, bckPin, wsPin, mclkPin, i2sDepth);
}

bool ESP32Synth::begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, I2S_Depth i2sDepth) {
    return begin(dataPin, mode, clkPin, wsPin, -1, i2sDepth);
}

bool ESP32Synth::begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, int mclkPin, I2S_Depth i2sDepth) {
    end();

    this->currentMode = mode;
    this->_i2sDepth   = i2sDepth;
    this->_dataPin    = dataPin;
    this->_bckPin     = clkPin;
    this->_wsPin      = wsPin;
    this->_mclkPin    = mclkPin;

    if (mode == SMODE_DAC) {
        _sampleRate       = 48000;
        _customSampleRate = false;
    } else if (mode == SMODE_PDM) {
        #if defined(CONFIG_IDF_TARGET_ESP32S3)
        _sampleRate = 52036;
        #else
        _sampleRate = 48000;
        #endif
        _customSampleRate = false;
    } else if (mode == SMODE_PWM) {
        // GOLDEN MATH: Absolute Phase-Lock!
        _sampleRate       = 47962;
        _customSampleRate = false;
    } else if (!_customSampleRate) {
        _sampleRate = 48000;
    }

    controlIntervalSamples = (_sampleRate / controlRateHz) ? (_sampleRate / controlRateHz) : 1;

    for (int i = 0; i < SINE_LUT_SIZE; i++) {
        sineLUT[i] = (int16_t)(sin(i * 2.0 * PI / (double)SINE_LUT_SIZE) * 32767.0);
    }

    if (mode == SMODE_DAC) {
        #if !defined(CONFIG_IDF_TARGET_ESP32)
        return false;
        #else
        dac_continuous_config_t cont_cfg = {
            .chan_mask  = (dataPin == 26) ? DAC_CHANNEL_MASK_CH1 : DAC_CHANNEL_MASK_CH0,
            .desc_num   = SYNTH_DMA_BUF_COUNT,
            .buf_size   = SYNTH_DMA_BUF_LEN * 2,
            .freq_hz    = _sampleRate * 2,
            .offset     = 0,
            .clk_src    = DAC_DIGI_CLK_SRC_DEFAULT,
            .chan_mode  = DAC_CHANNEL_MODE_SIMUL,
        };
        if (dac_continuous_new_channels(&cont_cfg, &dac_handle) != ESP_OK) return false;
        if (dac_continuous_enable(dac_handle)                   != ESP_OK) return false;
        #endif
    } else if (mode == SMODE_PWM) {
        ledc_timer_config_t ledc_timer   = {};
        ledc_timer.speed_mode            = SYNTH_PWM_MODE;
        ledc_timer.duty_resolution       = LEDC_TIMER_10_BIT;
        ledc_timer.timer_num             = LEDC_TIMER_0;
        ledc_timer.freq_hz               = _sampleRate;
        ledc_timer.clk_cfg               = LEDC_USE_APB_CLK;
        if (ledc_timer_config(&ledc_timer) != ESP_OK) return false;

        ledc_channel_config_t ledc_channel = {};
        ledc_channel.gpio_num              = dataPin;
        ledc_channel.speed_mode            = SYNTH_PWM_MODE;
        ledc_channel.channel               = LEDC_CHANNEL_0;
        ledc_channel.intr_type             = LEDC_INTR_DISABLE;
        ledc_channel.timer_sel             = LEDC_TIMER_0;
        ledc_channel.duty                  = 512;
        ledc_channel.hpoint                = 0;
        if (ledc_channel_config(&ledc_channel) != ESP_OK) return false;

        pwm_sema = xSemaphoreCreateBinary();

        // THE DEFINITIVE SECRET: Goodbye GPTimer! We hook our ISR directly to the LEDC pin.
        esp_intr_alloc(ETS_LEDC_INTR_SOURCE, ESP_INTR_FLAG_IRAM, ledc_ovf_isr, this, (intr_handle_t*)&pwm_timer);

        #if defined(CONFIG_IDF_TARGET_ESP32)
            REG_WRITE(LEDC_INT_ENA_REG, REG_READ(LEDC_INT_ENA_REG) | LEDC_HSTIMER0_OVF_INT_ENA);
        #else
            REG_WRITE(LEDC_INT_ENA_REG, REG_READ(LEDC_INT_ENA_REG) | LEDC_LSTIMER0_OVF_INT_ENA);
        #endif

    } else {
        i2s_port_t requested_port = (mode == SMODE_PDM) ? I2S_NUM_0 : I2S_NUM_AUTO;
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(requested_port, I2S_ROLE_MASTER);

        chan_cfg.dma_desc_num  = SYNTH_DMA_BUF_COUNT;
        chan_cfg.dma_frame_num = SYNTH_DMA_BUF_LEN;

        if (i2s_new_channel(&chan_cfg, &tx_handle, NULL) != ESP_OK) return false;

        if (mode == SMODE_PDM) {
            i2s_pdm_tx_config_t pdm_cfg = {
                .clk_cfg  = I2S_PDM_TX_CLK_DEFAULT_CONFIG(_sampleRate),
                .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
                .gpio_cfg = { .clk = (gpio_num_t)clkPin, .dout = (gpio_num_t)dataPin }
            };
            if (i2s_channel_init_pdm_tx_mode(tx_handle, &pdm_cfg) != ESP_OK) return false;
        } else {
            i2s_data_bit_width_t width = (i2sDepth == I2S_32BIT) ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT;
            i2s_std_config_t std_cfg = {
                .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(_sampleRate),
                .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(width, I2S_SLOT_MODE_STEREO),
                .gpio_cfg = {
                    .mclk = (mclkPin >= 0) ? (gpio_num_t)mclkPin : I2S_GPIO_UNUSED,
                    .bclk = (gpio_num_t)clkPin,
                    .ws   = (gpio_num_t)wsPin,
                    .dout = (gpio_num_t)dataPin
                }
            };
            if (i2s_channel_init_std_mode(tx_handle, &std_cfg) != ESP_OK) return false;
        }

        if (i2s_channel_enable(tx_handle) != ESP_OK) return false;
    }

    this->_running = true;

    // Core 1 on the S3 has less contention from USB CDC or Wi-Fi than Core 0.
    // With the mathematical Phase-Lock solved, Core 1 is the best place of all.
    if (xTaskCreatePinnedToCore(audioTask, "SynthTask", 4096, this, configMAX_PRIORITIES - 1, &audioTaskHandle, 1) != pdPASS) {
        return false;
    }

    return true;
}

bool ESP32Synth::beginCustom(uint32_t sampleRate, SynthCustomOutputCallback customOutput) {
    end();

    this->currentMode       = SMODE_CUSTOM;
    this->_sampleRate       = sampleRate;
    this->_customSampleRate = (sampleRate != 48000);
    this->_customOutput     = customOutput;

    controlIntervalSamples = (_sampleRate / controlRateHz) ? (_sampleRate / controlRateHz) : 1;

    for (int i = 0; i < SINE_LUT_SIZE; i++) {
        sineLUT[i] = (int16_t)(sin(i * 2.0 * PI / (double)SINE_LUT_SIZE) * 32767.0);
    }

    this->_running = true;

    // OPTIMIZATION: If the user provided a callback, the lib takes control and runs
    // the AudioTask in push mode. If NOT, no task is created — the user pulls data
    // on demand via generateSamples() (pull mode). Ideal for A2DP / Wi-Fi.
    if (customOutput != nullptr) {
        if (xTaskCreatePinnedToCore(audioTask, "SynthTask", 4096, this, configMAX_PRIORITIES - 1, &audioTaskHandle, 1) != pdPASS) {
            return false;
        }
    }

    return true;
}