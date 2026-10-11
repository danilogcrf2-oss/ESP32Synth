// Ative o modo **ultra low ram** no **ESP32Synth.h** para funcionar no CYD.
// Verifique se o seu LVGL está configurado.


#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include "ESP32Synth.h" 

// Pinos CYD
#define XPT_IRQ 36
#define XPT_MOSI 32
#define XPT_MISO 39
#define XPT_CLK 25
#define XPT_CS 33
#define BACKLIGHT_PIN 21
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS 5

// Pinos DAC I2S
#define I2S_BCK 22
#define I2S_WS 27
#define I2S_DATA 3

// Limites (Reduzidos para caber na RAM do ESP32)
#define MAX_SONGS 100
#define MAX_FILENAME_LEN 100

// Objetos
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSpi = SPIClass(VSPI); 
XPT2046_Touchscreen ts(XPT_CS, XPT_IRQ);
SPIClass sdSpi = SPIClass(HSPI);
ESP32Synth synth;

// Buffer LVGL (Reduzido para caber na RAM)
#define DRAW_BUF_SIZE (320 * 240 / 60)
uint8_t draw_buf[DRAW_BUF_SIZE];

// Variáveis do Player
char playlist[MAX_SONGS][MAX_FILENAME_LEN];
int currentTrack = 0;
int totalTracks = 0;
bool isPlaying = false;
uint8_t currentVolume = 127; // Inicia em 50%

// Objetos da UI LVGL Globais
lv_obj_t * title_label;
lv_obj_t * time_label;
lv_obj_t * slider_progress;
lv_obj_t * slider_volume;
lv_obj_t * btn_play_label;
lv_obj_t * switch_loop;
lv_obj_t * slider_time_label; // O novo indicador flutuante

// --- Funções de Display e Touch ---
void my_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, true);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

void my_touchpad_read(lv_indev_t * indev, lv_indev_data_t * data) {
    static int32_t last_x = 0;
    static int32_t last_y = 0;
    if(ts.touched()) {
        TS_Point p = ts.getPoint();
        last_x = map(p.x, 245, 3760, 0, 320);
        last_y = map(p.y, 308, 3740, 0, 240);
        last_x = constrain(last_x, 0, 320);
        last_y = constrain(last_y, 0, 240);
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

// --- Leitura do SD Card ---
void scanSDCard() {
    Serial.println("Buscando musicas...");
    totalTracks = 0;
    File root = SD.open("/");
    if(!root) return;
    
    while(true) {
        File entry = root.openNextFile();
        if(!entry) break; 
        
        if(!entry.isDirectory()) {
            String name = entry.name();
            String nameUpper = name;
            nameUpper.toUpperCase();
            if(nameUpper.endsWith(".WAV")) {
                String fullName = name.startsWith("/") ? name : "/" + name;
                strncpy(playlist[totalTracks], fullName.c_str(), MAX_FILENAME_LEN - 1);
                playlist[totalTracks][MAX_FILENAME_LEN - 1] = '\0'; 
                totalTracks++;
                if(totalTracks >= MAX_SONGS) break; 
            }
        }
        entry.close();
    }
    root.close();
}

// --- Funções de Controle de Áudio ---
void loadTrack(int index) {
    if(totalTracks == 0) return;
    synth.stopStream(0);
    lv_label_set_text(title_label, playlist[index]);
    
    synth.playStream(0, SD, playlist[index], currentVolume, 26163, false);
    isPlaying = true;
    lv_label_set_text(btn_play_label, "Pause");
}

static void btn_play_event_cb(lv_event_t * e) {
    if(totalTracks == 0) return;
    if(isPlaying) {
        synth.pauseStream(0);
        isPlaying = false;
        lv_label_set_text(btn_play_label, "Play");
    } else {
        synth.resumeStream(0);
        isPlaying = true;
        lv_label_set_text(btn_play_label, "Pause");
    }
}

static void btn_next_event_cb(lv_event_t * e) {
    if(totalTracks == 0) return;
    currentTrack = (currentTrack + 1) % totalTracks;
    loadTrack(currentTrack);
}

static void btn_prev_event_cb(lv_event_t * e) {
    if(totalTracks == 0) return;
    currentTrack = (currentTrack - 1 + totalTracks) % totalTracks;
    loadTrack(currentTrack);
}

static void slider_progress_event_cb(lv_event_t * e) {
    if(totalTracks == 0) return;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * slider = (lv_obj_t*) lv_event_get_target(e);
    uint32_t duration = synth.getStreamDurationMs(0);

    if(duration > 0) {
        // Se estiver apertando ou movendo o dedo
        if(code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {
            lv_obj_clear_flag(slider_time_label, LV_OBJ_FLAG_HIDDEN); // Mostra o label
            
            uint32_t targetMs = (lv_slider_get_value(slider) * duration) / 100;
            uint32_t targetSec = targetMs / 1000;
            
            char time_str[10];
            snprintf(time_str, sizeof(time_str), "%02d:%02d", targetSec / 60, targetSec % 60);
            lv_label_set_text(slider_time_label, time_str);
        }
        // Quando soltar o dedo
        else if(code == LV_EVENT_RELEASED) {
            lv_obj_add_flag(slider_time_label, LV_OBJ_FLAG_HIDDEN); // Esconde o label
            
            uint32_t targetMs = (lv_slider_get_value(slider) * duration) / 100;
            synth.seekStreamMs(0, targetMs); // Pula a música
        }
    }
}

static void slider_volume_event_cb(lv_event_t * e) {
    lv_obj_t * slider = (lv_obj_t*) lv_event_get_target(e);
    currentVolume = (uint8_t)lv_slider_get_value(slider);
    synth.setMasterVolume(currentVolume); 
}

// --- Construção da UI Integrada ---
void build_ui() {
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x101010), LV_PART_MAIN);

    title_label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffc107), LV_PART_MAIN);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(title_label, 280);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 15);
    lv_label_set_text(title_label, "Carregando SD...");

    time_label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(time_label, "00:00 / 00:00");

    slider_progress = lv_slider_create(lv_screen_active());
    lv_obj_set_size(slider_progress, 300, 10);
    lv_obj_align(slider_progress, LV_ALIGN_TOP_MID, 0, 85);
    lv_slider_set_range(slider_progress, 0, 100);
    lv_obj_set_style_bg_color(slider_progress, lv_color_hex(0x3f51b5), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_progress, lv_color_hex(0x3f51b5), LV_PART_KNOB);
    
    // Passamos a escutar TODOS os eventos para a mágica de esconder/mostrar funcionar
    lv_obj_add_event_cb(slider_progress, slider_progress_event_cb, LV_EVENT_ALL, NULL);

    // Criação do novo Label Flutuante que você pediu (Inicia escondido)
    slider_time_label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_color(slider_time_label, lv_color_hex(0xff5722), LV_PART_MAIN);
    lv_obj_align_to(slider_time_label, slider_progress, LV_ALIGN_OUT_TOP_MID, 0, -5); 
    lv_label_set_text(slider_time_label, "00:00");
    lv_obj_add_flag(slider_time_label, LV_OBJ_FLAG_HIDDEN); 

    lv_obj_t * btn_prev = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn_prev, 40, 40);
    lv_obj_align(btn_prev, LV_ALIGN_CENTER, -60, 15);
    lv_obj_set_style_radius(btn_prev, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_prev, btn_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * label_prev = lv_label_create(btn_prev);
    lv_label_set_text(label_prev, "<");
    lv_obj_center(label_prev);

    lv_obj_t * btn_play = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn_play, 70, 40);
    lv_obj_align(btn_play, LV_ALIGN_CENTER, 0, 15);
    lv_obj_set_style_radius(btn_play, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_play, btn_play_event_cb, LV_EVENT_CLICKED, NULL);
    btn_play_label = lv_label_create(btn_play);
    lv_label_set_text(btn_play_label, "Play");
    lv_obj_center(btn_play_label);

    lv_obj_t * btn_next = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn_next, 40, 40);
    lv_obj_align(btn_next, LV_ALIGN_CENTER, 60, 15);
    lv_obj_set_style_radius(btn_next, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_next, btn_next_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * label_next = lv_label_create(btn_next);
    lv_label_set_text(label_next, ">");
    lv_obj_center(label_next);

    switch_loop = lv_switch_create(lv_screen_active());
    lv_obj_align(switch_loop, LV_ALIGN_BOTTOM_LEFT, 15, -20);
    
    lv_obj_t * label_loop = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_color(label_loop, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align_to(label_loop, switch_loop, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_label_set_text(label_loop, "Loop");

    slider_volume = lv_slider_create(lv_screen_active());
    lv_obj_set_size(slider_volume, 110, 7);
    lv_obj_align(slider_volume, LV_ALIGN_BOTTOM_RIGHT, -15, -24);
    lv_slider_set_range(slider_volume, 0, 255);
    lv_slider_set_value(slider_volume, currentVolume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_volume, lv_color_hex(0x3eef3f), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_volume, lv_color_hex(0x3eef3f), LV_PART_KNOB);
    lv_obj_add_event_cb(slider_volume, slider_volume_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * label_vol = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_color(label_vol, lv_color_hex(0xeeeeee), LV_PART_MAIN);
    lv_obj_align_to(label_vol, slider_volume, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    lv_label_set_text(label_vol, "Vol:");
}

void setup() {
    Serial.begin(115200);

    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.invertDisplay(true);
    
    touchSpi.begin(XPT_CLK, XPT_MISO, XPT_MOSI, XPT_CS);
    ts.begin(touchSpi);
    ts.setRotation(1);

    sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if(!SD.begin(SD_CS, sdSpi, 16000000)) {
        Serial.println("Erro no SD!");
    }

    synth.begin(I2S_BCK, I2S_WS, I2S_DATA, I2S_32BIT);
    synth.setMasterVolume(currentVolume);

    lv_init();
    lv_display_t * disp = lv_display_create(320, 240);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);

    build_ui();
    
    scanSDCard();
    if(totalTracks > 0) {
        loadTrack(0); 
    } else {
        lv_label_set_text(title_label, "SD Vazio ou s/ .wav");
    }
}

void loop() {
    lv_tick_inc(1); 
    lv_timer_handler(); 
    
    if(isPlaying && !lv_obj_has_state(slider_progress, LV_STATE_PRESSED)) {
        uint32_t pos = synth.getStreamPositionMs(0);
        uint32_t dur = synth.getStreamDurationMs(0);
        
        if(dur > 0) {
            int percentage = (pos * 100) / dur;
            lv_slider_set_value(slider_progress, percentage, LV_ANIM_OFF);

            char time_str[32];
            uint32_t pos_sec = pos / 1000;
            uint32_t dur_sec = dur / 1000;
            snprintf(time_str, sizeof(time_str), "%02d:%02d / %02d:%02d", 
                     pos_sec / 60, pos_sec % 60, 
                     dur_sec / 60, dur_sec % 60);
            lv_label_set_text(time_label, time_str);
        }
        
        if(!synth.isStreamPlaying(0) && pos > 0 && totalTracks > 0) { 
            if(lv_obj_has_state(switch_loop, LV_STATE_CHECKED)) {
                loadTrack(currentTrack); 
            } else {
                currentTrack = (currentTrack + 1) % totalTracks;
                loadTrack(currentTrack); 
            }
        }
    }
    
    delay(1);
}