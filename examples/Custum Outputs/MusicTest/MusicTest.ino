// FOR THIS TO WORK, PLEASE USE ESP32 CORE (Arduino IDE) 3.0.0 TO 3.1.3 

#include <Arduino.h>
#include <ESP32Synth.h>
#include "BluetoothA2DPSource.h"
#include "esp_gap_bt_api.h"
#include "esp_bt.h" 
BluetoothA2DPSource a2dp_source;
ESP32Synth synth;

// ====================================================================================
// == MODULE 1: BLUETOOTH A2DP MANAGER (Otimizado para economia de DRAM e alta potência)
// ====================================================================================

#define MAX_DISCOVERED_DEVICES 15
#define PRINT_INTERVAL_MS      3000

struct DiscoveredDevice {
    uint8_t address[6];
    char name[64];
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

bool bt_device_discovered_cb(const char* ssid, esp_bd_addr_t address, int rssi) {
    if (ssid == nullptr || strlen(ssid) == 0) return false;
    for (uint8_t i = 0; i < device_count; i++) {
        if (memcmp(devices[i].address, address, 6) == 0) {
            devices[i].rssi = rssi;
            return false; 
        }
    }

    if (device_count < MAX_DISCOVERED_DEVICES) {
        memcpy(devices[device_count].address, address, 6);
        strncpy(devices[device_count].name, ssid, sizeof(devices[device_count].name) - 1);
        devices[device_count].name[sizeof(devices[device_count].name) - 1] = '\0';
        devices[device_count].rssi = rssi;
        device_count++;
    }
    return false; 
}

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

void bt_connection_state_cb(esp_a2d_connection_state_t state, void *ptr) {
    switch (state) {
        case ESP_A2D_CONNECTION_STATE_CONNECTING:
            Serial.println("[A2DP] Connecting to target device...");
            break;
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            Serial.println("[A2DP] Connection successful! DSP active.");
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
    // 1. Movemos a renderização pesada para o Core 1 para não travar o rádio no Core 0
    a2dp_source.set_task_core(1);
    a2dp_source.set_task_priority(configMAX_PRIORITIES - 3);

    a2dp_source.set_auto_reconnect(false);
    a2dp_source.set_on_connection_state_changed(bt_connection_state_cb);
    a2dp_source.set_ssid_callback(bt_device_discovered_cb);
    a2dp_source.set_discovery_mode_callback(bt_discovery_mode_cb);
    a2dp_source.set_volume(85);

    device_count = 0;
    a2dp_source.start();

    // 2. Maximiza a potência de transmissão do rádio (+9dBm) para eliminar jitter por sinal fraco
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
        esp_bt_gap_cancel_discovery(); 
        is_scanning = false;
        a2dp_source.connect_to(selected_address);
    }

    if (scan_needs_restart) {
        scan_needs_restart = false;
        bt_start_scan();
    }
}

int32_t get_data_frames(Frame *frame, int32_t frame_count) {
    if (__builtin_expect(frame == nullptr || frame_count <= 0, 0)) return 0;
    
    synth.generateSamplesStereo((int16_t*)frame, frame_count);
    return frame_count;
}

// Canalização das vozes do Synth
const int CH_BASS_SAW = 0;
const int CH_BASS_SUB = 1; 
const int CH_CHORDS   = 2; 
const int CH_LEAD     = 7;
const int CH_ARP      = 8;
const int CH_COUNTER  = 9; 

// 3. Reduzido para 8000 para economizar 48KB de RAM interna crítica para o Bluetooth!
#define TAPE_LEN 8000 
int32_t* abyssTape = nullptr;
int writeHead = 0;

int32_t lpState = 0;

void IRAM_ATTR theAbyssDSP(int32_t* mixBuffer, int numSamples) {
    if (!abyssTape) return; 

    for (int i = 0; i < numSamples; i++) {
        int32_t dry = mixBuffer[i];

        int tap1 = writeHead - 1327;  if (tap1 < 0) tap1 += TAPE_LEN;
        int tap2 = writeHead - 3003;  if (tap2 < 0) tap2 += TAPE_LEN;
        int tap3 = writeHead - 7013;  if (tap3 < 0) tap3 += TAPE_LEN;

        int32_t wet = (abyssTape[tap1] >> 2) + (abyssTape[tap2] >> 2) + (abyssTape[tap3] >> 2); 

        lpState = ((wet * 50) + (lpState * 206)) >> 8; 

        int32_t feedback = (dry >> 1) + ((lpState * 200) >> 8);

        if (feedback > 32767) feedback = 32767;
        else if (feedback < -32768) feedback = -32768;

        abyssTape[writeHead] = feedback;
        writeHead++;
        if (writeHead >= TAPE_LEN) writeHead = 0;

        mixBuffer[i] = (dry) + (lpState << 2);
    }
}

void setup(){
  Serial.begin(115200);
  delay(1000);
  Serial.println("[System] Initializing ESP32Synth & Bluetooth...");

  synth.beginCustom(44100, nullptr);
  synth.setMasterVolume(80);

  // Alocação otimizada
  abyssTape = (int32_t*)heap_caps_calloc(TAPE_LEN, sizeof(int32_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  synth.setCustomDSP(theAbyssDSP);

  synth.setWave(CH_BASS_SAW, WAVE_SAW);
  synth.setWave(CH_BASS_SUB, WAVE_SINE);
  
  for(int i = 0; i < 5; i++){
    synth.setWave(CH_CHORDS + i, WAVE_PULSE);
    synth.setPulseWidth(CH_CHORDS + i, 128); 
    synth.setEnv(CH_CHORDS + i, 50, 100, 150, 600); 
  }

  synth.setWave(CH_ARP, WAVE_TRIANGLE);
  synth.setEnv(CH_ARP, 10, 50, 200, 100);
  synth.setWave(CH_LEAD, WAVE_PULSE);
  synth.setPulseWidth(CH_LEAD, 64);
  synth.setEnv(CH_LEAD, 20, 100, 200, 400); 
  synth.setVibrato(CH_LEAD, 500, 15); 

  synth.setWave(CH_COUNTER, WAVE_SINE);
  synth.setEnv(CH_COUNTER, 400, 0, 255, 800);

  a2dp_source.set_data_callback_in_frames(get_data_frames);
  bt_setup();
}

void chord(uint32_t bs, uint32_t n1, uint32_t n2, uint32_t n3, uint32_t n4, uint32_t n5) {
  synth.noteOn(CH_BASS_SAW, bs, 100);
  synth.noteOn(CH_BASS_SUB, bs, 40);
  synth.noteOn(CH_CHORDS + 0, n1, 25);
  synth.noteOn(CH_CHORDS + 1, n2, 25);
  synth.noteOn(CH_CHORDS + 2, n3, 25);
  synth.noteOn(CH_CHORDS + 3, n4, 25);
  synth.noteOn(CH_CHORDS + 4, n5, 25);
}

void chordOff() {
  synth.noteOff(CH_BASS_SAW);
  synth.noteOff(CH_BASS_SUB);
  for(int i = 0; i < 5; i++) { synth.noteOff(CH_CHORDS + i); }
}

void part1_Intro() {
  chord(cs2, cs4, e4, gs4, cs5, e5);
  synth.setArpeggio(CH_ARP, 120, cs4, e4, gs4, cs5, gs4, e4);
  synth.noteOn(CH_ARP, cs4, 50);
  delay(4000); chordOff(); synth.noteOff(CH_ARP); delay(500);

  chord(ds2, ds4, fs4, a4, ds5, fs5);
  synth.setArpeggio(CH_ARP, 120, ds4, fs4, a4, ds5, a4, fs4);
  synth.noteOn(CH_ARP, ds4, 50);
  delay(4000); chordOff(); synth.noteOff(CH_ARP); delay(500);

  chord(e2, cs4, e4, gs4, cs5, e5);
  synth.setArpeggio(CH_ARP, 120, e4, gs4, b4, e5, b4, gs4);
  synth.noteOn(CH_ARP, e4, 50);
  delay(4000); chordOff(); synth.noteOff(CH_ARP); delay(500);
  synth.noteOn(CH_BASS_SAW, e2, 100);
  synth.noteOn(CH_BASS_SUB, e2, 40);
  synth.slideFreqTo(CH_BASS_SAW, gs2, 1000); 
  synth.slideFreqTo(CH_BASS_SUB, gs2, 1000);
  delay(1000);
  
  chord(gs2, c4, ds4, fs4, gs4, c5); 
  synth.setArpeggio(CH_ARP, 60, gs4, c5, ds5, fs5); 
  synth.noteOn(CH_ARP, gs4, 80);
  synth.noteOn(CH_COUNTER, gs3, 0); 
  synth.slideVolTo(CH_COUNTER, 100, 2000);
  synth.slideFreqTo(CH_COUNTER, ds5, 2000);
  delay(3000);
  
  chordOff();
  synth.detachArpeggio(CH_ARP);
  synth.noteOff(CH_ARP);
  synth.noteOff(CH_COUNTER);
  delay(400); 
}

void part2_Improv_Csm() {
  chord(cs2, cs4, e4, gs4, cs5, e5);
  synth.setArpeggio(CH_ARP, 100, cs4, e4, gs4, cs5, gs4, e4);
  synth.noteOn(CH_ARP, cs4, 40);
  
  synth.noteOn(CH_COUNTER, e4, 70); 
  synth.noteOn(CH_LEAD, gs4, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, e4, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, cs5, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, b4, 150); delay(1000);
  
  chordOff(); synth.noteOff(CH_COUNTER); synth.noteOff(CH_LEAD); delay(100);

  chord(ds2, ds4, fs4, a4, ds5, fs5);
  synth.noteOn(CH_COUNTER, a4, 70); 

  synth.noteOn(CH_LEAD, a4, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, fs4, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, ds5, 300); delay(1000); 
  synth.slideFreqTo(CH_LEAD, cs5, 150); delay(1000);

  chordOff(); synth.noteOff(CH_COUNTER); synth.noteOff(CH_LEAD); delay(100);

  chord(e2, cs4, e4, gs4, cs5, e5);
  synth.noteOn(CH_COUNTER, b4, 70); 

  synth.noteOn(CH_LEAD, gs4, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, b4, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, e5, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, ds5, 150); delay(1000);

  chordOff(); synth.noteOff(CH_COUNTER); synth.noteOff(CH_LEAD); delay(100);

  chord(gs2, c4, ds4, fs4, gs4, c5);
  synth.noteOn(CH_COUNTER, c5, 80); 

  synth.noteOn(CH_LEAD, c5, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, gs4, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, fs5, 400); delay(1000); 
  synth.slideFreqTo(CH_LEAD, e5, 200); delay(1000); 

  chordOff(); synth.noteOff(CH_COUNTER); synth.noteOff(CH_LEAD);
  synth.detachArpeggio(CH_ARP); synth.noteOff(CH_ARP);
  delay(300);
}

void part3_Modulation_Fsm() {
  chord(fs2, cs4, fs4, a4, cs5, fs5);
  synth.setArpeggio(CH_ARP, 90, fs4, a4, cs5, fs5, cs5, a4);
  synth.noteOn(CH_ARP, fs4, 50);
  
  synth.noteOn(CH_COUNTER, cs5, 70); 

  synth.noteOn(CH_LEAD, a4, 160); delay(1000);
  synth.slideFreqTo(CH_LEAD, fs4, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, e5, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, ds5, 150); delay(1000);
  
  chordOff(); synth.noteOff(CH_COUNTER); synth.noteOff(CH_LEAD); delay(100);
  chord(gs2, d4, f4, gs4, d5, f5); 
  synth.noteOn(CH_COUNTER, d5, 70); 

  synth.noteOn(CH_LEAD, b4, 160); delay(1000);
  synth.slideFreqTo(CH_LEAD, gs4, 150); delay(1000);
  synth.slideFreqTo(CH_LEAD, f5, 300); delay(1000);
  synth.slideFreqTo(CH_LEAD, d5, 150); delay(1000);

  chordOff(); synth.noteOff(CH_COUNTER); synth.noteOff(CH_LEAD);
  synth.detachArpeggio(CH_ARP); synth.noteOff(CH_ARP);
  delay(1000);
}

void part4_Finalization() {

  chord(a2, e4, a4, cs5, e5, a5);
  synth.setArpeggio(CH_ARP, 100, a4, cs5, e5, a5, e5, cs5);
  synth.noteOn(CH_ARP, a4, 50);

  synth.noteOn(CH_COUNTER, a5, 60); 

  synth.noteOn(CH_LEAD, cs5, 160); delay(1000);
  synth.slideFreqTo(CH_LEAD, e5, 2000); delay(2000);
  
  chordOff(); synth.noteOff(CH_COUNTER); synth.noteOff(CH_LEAD); delay(100);
  chord(b2, ds4, fs4, b4, ds5, fs5);
  synth.setArpeggio(CH_ARP, 90, b4, ds5, fs5, b5, fs5, ds5);
  synth.noteOn(CH_ARP, b4, 55);
  
  synth.noteOn(CH_COUNTER, b5, 65); 

  synth.noteOn(CH_LEAD, ds5, 160); delay(1000);
  synth.slideFreqTo(CH_LEAD, fs5, 2000); delay(2000); 
  
  chordOff(); synth.noteOff(CH_COUNTER); synth.noteOff(CH_LEAD); delay(100);

  chord(cs2, gs4, cs5, fs5, gs5, cs6);
  synth.setArpeggio(CH_ARP, 60, cs5, fs5, gs5, cs6); 
  synth.noteOn(CH_ARP, cs5, 70);
  synth.noteOn(CH_COUNTER, gs5, 70); 
  synth.noteOn(CH_LEAD, fs5, 180); 
  delay(3000); 
  chord(cs2, gs4, cs5, f5, gs5, cs6); 
  synth.setArpeggio(CH_ARP, 150, cs4, f4, gs4, cs5);
  synth.slideFreqTo(CH_LEAD, f5, 1000); 
  delay(2000);

  for(int i = 0; i < 10; i++) {
    synth.slideVolTo(i, 0, 4000);
  }
  
  delay(4500); 
  
  chordOff(); 
  synth.noteOff(CH_LEAD);
  synth.noteOff(CH_COUNTER);
  synth.detachArpeggio(CH_ARP); 
  synth.noteOff(CH_ARP);
  delay(2000); 
}

void loop(){
  bt_loop();
  
  if(a2dp_source.is_connected()){
    part1_Intro();
    part2_Improv_Csm();
    part3_Modulation_Fsm();
    part4_Finalization();
  } else {
    delay(5); // Non-blocking delay when scanning to allow immediate Serial process
  }
}