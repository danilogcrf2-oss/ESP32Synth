/*
Critical Memory Configuration Warning
If this example fails to compile, overflows DRAM, or crashes during Bluetooth initialization, you must downgrade your ESP32 Arduino Board Core in the Boards Manager to a stable version between 3.0.0 and 3.1.3.
Why does this happen?
Core 3.2.0+ overhead: Starting with Arduino Core version 3.2.0, Espressif introduced massive internal driver layers and static system buffers. This significantly increases static DRAM usage, leaving almost no free heap for classic Bluetooth (Bluedroid) on the standard ESP32.
Snyth Compatibility: This is not a limitation or issue with ESP32Synth. The synthesizer is fully compatible with all board core versions from 3.0.0 up to the absolute latest. Downgrading to a core version between 3.0.0 and 3.1.3 simply reclaims over 30 KB of vital RAM for your application.
Low-RAM Interactive List Example
This version provides the fully interactive Serial list interface, heavily optimized to consume the absolute minimum RAM possible:
Compact Registry: Limits the maximum discovered devices to 8 and caps device name buffers to 32 characters. This limits the memory footprint of the device table to less than 350 bytes.
No Heap Allocation Bloat: Avoids large delay buffers or dynamic arrays in the audio rendering block.
Task Segregation: Moves the A2DP synthesis queue to Core 1, keeping Core 0 clean to handle the low-level RF radio stack without audio jitter.
Non-Blocking Serial Parser: Bypasses Serial.parseInt() entirely to read input character-by-character, preventing blocking-state lag on Core 1 while the synthesizer is playing.
*/

#include <Arduino.h>
#include <ESP32Synth.h>
#include "BluetoothA2DPSource.h"
#include "esp_gap_bt_api.h"
#include "esp_bt.h"

BluetoothA2DPSource a2dp_source;
ESP32Synth synth;

// Non-blocking chord sequence variables
unsigned long lastChangeTime = 0;
uint8_t chordIndex = 0;

// ====================================================================================
// == MODULE 1: MEMORY-OPTIMIZED BLUETOOTH A2DP MANAGER
// ====================================================================================

#define MAX_DISCOVERED_DEVICES 8
#define PRINT_INTERVAL_MS      3000

struct DiscoveredDevice {
    uint8_t address[6];
    char name[32]; // Capped to save vital stack/heap RAM
    int rssi;
};

DiscoveredDevice devices[MAX_DISCOVERED_DEVICES];
uint8_t device_count = 0;
unsigned long last_print_time = 0;
bool is_scanning = false;
bool is_connecting = false;
esp_bd_addr_t selected_address;

char serial_buf[16];
uint8_t serial_buf_idx = 0;

volatile bool trigger_connect = false;
volatile bool scan_needs_restart = false;

// Callback triggered for every unique valid audio sink (speaker) discovered
bool bt_device_discovered_cb(const char* ssid, esp_bd_addr_t address, int rssi) {
    if (ssid == nullptr || strlen(ssid) == 0) return false;

    // Check for duplicates
    for (uint8_t i = 0; i < device_count; i++) {
        if (memcmp(devices[i].address, address, 6) == 0) {
            devices[i].rssi = rssi; // Update RSSI
            return false; 
        }
    }

    // Add unique device if the table has empty slots
    if (device_count < MAX_DISCOVERED_DEVICES) {
        memcpy(devices[device_count].address, address, 6);
        strncpy(devices[device_count].name, ssid, sizeof(devices[device_count].name) - 1);
        devices[device_count].name[sizeof(devices[device_count].name) - 1] = '\0';
        devices[device_count].rssi = rssi;
        device_count++;
    }
    return false; // Continue scanning the full window
}

// Callback tracking GAP discovery states
void bt_discovery_mode_cb(esp_bt_gap_discovery_state_t state) {
    if (state == ESP_BT_GAP_DISCOVERY_STARTED) {
        is_scanning = true;
    } else if (state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        is_scanning = false;
        if (!a2dp_source.is_connected() && !is_connecting) {
            scan_needs_restart = true;
        }
    }
}

// Callback tracking A2DP connection states
void bt_connection_state_cb(esp_a2d_connection_state_t state, void *ptr) {
    switch (state) {
        case ESP_A2D_CONNECTION_STATE_CONNECTING:
            Serial.println("[A2DP] Connecting to target device...");
            break;
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            Serial.println("[A2DP] Connection successful! Synth active.");
            is_connecting = false;
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            Serial.println("[A2DP] Disconnected. Returning to discovery cycle.");
            is_connecting = false;
            scan_needs_restart = true;
            break;
        default:
            break;
    }
}

void bt_setup() {
    // Process synthesis and streaming queue on Core 1, keeping Core 0 free for BT radio
    a2dp_source.set_task_core(1);
    a2dp_source.set_task_priority(configMAX_PRIORITIES - 3);

    a2dp_source.set_auto_reconnect(false);
    a2dp_source.set_on_connection_state_changed(bt_connection_state_cb);
    a2dp_source.set_ssid_callback(bt_device_discovered_cb);
    a2dp_source.set_discovery_mode_callback(bt_discovery_mode_cb);
    a2dp_source.set_volume(85);

    device_count = 0;
    a2dp_source.start();

    // Maximize radio TX power to prevent dropouts and audio jitter
    esp_bredr_tx_power_set(ESP_PWR_LVL_P9, ESP_PWR_LVL_P9);
}

void bt_start_scan() {
    if (is_scanning) return;
    device_count = 0;
    a2dp_source.start();
    is_scanning = true;
}

void bt_print_list() {
    Serial.println("\n==================================================================");
    Serial.println("            BLUETOOTH AUDIO DEVICES (SINKs) DISCOVERED            ");
    Serial.println("==================================================================");
    if (device_count == 0) {
        Serial.println("Scanning... No devices found nearby yet. (Is speaker in pairing mode?)");
    } else {
        for (uint8_t i = 0; i < device_count; i++) {
            Serial.printf("[%d] %-30s (%d dBm)  [%02X:%02X:%02X:%02X:%02X:%02X]\n",
                          i + 1,
                          devices[i].name,
                          devices[i].rssi,
                          devices[i].address[0], devices[i].address[1], devices[i].address[2],
                          devices[i].address[3], devices[i].address[4], devices[i].address[5]);
        }
        Serial.println("------------------------------------------------------------------");
        Serial.println("Type the index number of the device and press ENTER to connect.");
    }
    Serial.println("==================================================================\n");
}

// 100% Non-blocking, character-by-character Serial parser
void bt_handle_serial_input() {
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serial_buf_idx > 0) {
                serial_buf[serial_buf_idx] = '\0';
                int selection = atoi(serial_buf);
                serial_buf_idx = 0;
                
                if (selection >= 1 && selection <= device_count) {
                    uint8_t idx = selection - 1;
                    Serial.printf("[BT] Target selected: %s. Initiating connection...\n", devices[idx].name);
                    memcpy(selected_address, devices[idx].address, 6);
                    trigger_connect = true;
                } else {
                    Serial.println("[BT] Invalid selection number. Try again.");
                }
            }
        }
        else if (isdigit(c) && serial_buf_idx < sizeof(serial_buf) - 1) {
            serial_buf[serial_buf_idx++] = c;
        }
    }
}

void bt_loop() {
    unsigned long current_time = millis();
    
    if (!a2dp_source.is_connected() && !is_connecting) {
        if (current_time - last_print_time >= PRINT_INTERVAL_MS) {
            last_print_time = current_time;
            bt_print_list();
        }
        bt_handle_serial_input();
    }

    if (trigger_connect) {
        trigger_connect = false;
        is_connecting = true;
        esp_bt_gap_cancel_discovery(); // Stop GAP inquiry scanner cleanly
        is_scanning = false;
        a2dp_source.connect_to(selected_address);
    }

    if (scan_needs_restart) {
        scan_needs_restart = false;
        bt_start_scan();
    }
}

// ====================================================================================
// == MODULE 2: AUDIO DSP & MUSIC LOGIC
// ====================================================================================

int32_t get_data_frames(Frame *frame, int32_t frame_count) {
    if (__builtin_expect(frame == nullptr || frame_count <= 0, 0)) return 0;
    
    synth.generateSamplesStereo((int16_t*)frame, frame_count);
    return frame_count;
}

void synth_setup() {
    synth.beginCustom(44100, nullptr);
    synth.setMasterVolume(100);

    // Voice 0: Melody Lead (Pulse wave)
    synth.setWave(0, WAVE_PULSE);
    synth.setPulseWidth(0, 128); // Square wave
    synth.setVolume(0, 150);
    synth.setEnv(0, 10, 80, 120, 200);

    // Voice 1: Bass line (Triangle wave)
    synth.setWave(1, WAVE_TRIANGLE);
    synth.setVolume(1, 130);
    synth.setEnv(1, 40, 150, 180, 250);
}

void music_loop() {
    unsigned long currentMillis = millis();
    if (currentMillis - lastChangeTime >= 2000) {
        lastChangeTime = currentMillis;
        
        switch (chordIndex) {
            case 0:
                synth.noteOn(0, c4, 150);
                synth.noteOn(1, c2, 130);
                chordIndex = 1;
                break;
            case 1:
                synth.noteOn(0, e4, 150);
                synth.noteOn(1, a1, 130);
                chordIndex = 2;
                break;
            case 2:
                synth.noteOn(0, f4, 150);
                synth.noteOn(1, f1, 130);
                chordIndex = 3;
                break;
            case 3:
                synth.noteOn(0, g4, 150);
                synth.noteOn(1, g1, 130);
                chordIndex = 0;
                break;
        }
    }
}

// ====================================================================================
// == MODULE 3: MAIN SYSTEM ENTRYPOINTS
// ====================================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[System] Initializing Memory-Optimized Interactive A2DP...");

    synth_setup();

    a2dp_source.set_data_callback_in_frames(get_data_frames);
    bt_setup();
}

void loop() {
    // Process BT task manager states
    bt_loop();

    // Trigger music sequence if speaker is connected
    if (a2dp_source.is_connected()) {
        music_loop();
    } else {
        delay(5); // Non-blocking sleep keeping Core 1 watchdogs feeding smoothly
    }
}