/**
 * @file ESP32Synth_WavPlayer.ino
 * @brief Player de Arquivos WAV usando ESP32Synth
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Encoder.h>
#include <vector>
#include "ESP32Synth.h"

// ==========================================================
// == DEFINIÇÃO DE PINOS
// ==========================================================

// SD Card
#define SD_CS     5
#define SD_SCK    18
#define SD_MISO   19
#define SD_MOSI   23

// DAC I2S (PCM5102A)
#define I2S_BCK   4
#define I2S_WS    15
#define I2S_DIN   2

// Display OLED I2C (Pinos Padrões)
#define OLED_SDA  21
#define OLED_SCL  22

// Rotary Encoder (Módulo KY-040)
#define ENC_CLK   13 // Pino A (CLK)
#define ENC_DT    14 // Pino B (DT)
#define ENC_SW    27 // Botão do Encoder (SW)

// ==========================================================
// == CONFIGURAÇÕES E OBJETOS
// ==========================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ESP32Encoder encoder;
ESP32Synth synth;

// ==========================================================
// == VARIÁVEIS GLOBAIS E ESTADOS
// ==========================================================
std::vector<String> playlist;

enum UI_State {
    STATE_FILE_LIST,
    STATE_PLAYER
};
UI_State uiState = STATE_FILE_LIST;

// Controle de Navegação na Lista
int listSelectedIndex = 0;
int listScrollOffset = 0;

// Estados do Player (Atualizado com botão de voltar)
enum PlayerControl {
    CTRL_BACK = 0, // Botão de Voltar para a Lista
    CTRL_PREV,
    CTRL_REV5,
    CTRL_PLAY,
    CTRL_FWD5,
    CTRL_NEXT,
    CTRL_LOOP,
    CTRL_TIME,     // Slider de tempo
    CTRL_VOL,      // Slider de Volume
    CTRL_MAX
};

int playerFocusedCtrl = CTRL_PLAY;
bool isEditingSlider = false;

int currentTrack = 0;
bool isPlaying = false;
bool isLooping = false;
uint8_t currentVolume = 120; // Volume inicial (0-255)

// Variáveis para sliders
uint32_t seekTargetMs = 0;
uint32_t trackDurationMs = 0;

// Variáveis auxiliares para debounce e fluidez
int32_t lastEncoderCount = 0;
uint32_t lastButtonPress = 0;
bool buttonState = HIGH;

// ==========================================================
// == PROTÓTIPOS
// ==========================================================
void scanDirectory(File dir);
void playTrack(int index);
void handleInput();
void updatePlayerLogic();
void drawUI();
void drawFileList();
void drawPlayer();
void drawIcon(int x, int y, int type, bool filled);

// ==========================================================
// == SETUP
// ==========================================================
void setup() {
    Serial.begin(115200);

    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("Falha ao encontrar o OLED SSD1306");
        for (;;);
    }
    
    display.setTextWrap(false); 
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    
    display.setCursor(10, 20);
    display.println("Iniciando...");
    display.display();

    pinMode(ENC_SW, INPUT_PULLUP);

    // Inicialização do Encoder para o módulo KY-040
    ESP32Encoder::useInternalWeakPullResistors = puType::up;
    encoder.attachSingleEdge(ENC_DT, ENC_CLK); 
    encoder.setCount(0);

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 16000000)) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("Erro no SD Card!");
        display.display();
        while (1);
    }

    File root = SD.open("/");
    scanDirectory(root);
    root.close();

    if (playlist.empty()) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("Nenhum .WAV no SD!");
        display.display();
        while (1);
    }

    if (!synth.begin(I2S_BCK, I2S_WS, I2S_DIN, I2S_32BIT)) {
        Serial.println("Falha ao iniciar ESP32Synth");
        while (1);
    }
    synth.setMasterVolume(currentVolume);
}

// ==========================================================
// == LOOP PRINCIPAL
// ==========================================================
void loop() {
    handleInput();
    updatePlayerLogic();
    drawUI();
    vTaskDelay(pdMS_TO_TICKS(10)); 
}

// ==========================================================
// == LÓGICA DE ÁUDIO E PLAYER
// ==========================================================
void scanDirectory(File dir) {
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            String name = entry.name();
            String nameLower = name;
            nameLower.toLowerCase();
            if (nameLower.endsWith(".wav")) {
                playlist.push_back(name);
            }
        }
        entry.close();
    }
}

void playTrack(int index) {
    if (index < 0 || index >= playlist.size()) return;
    
    synth.stopStream(0);
    String path = "/" + playlist[index];
    
    synth.playStream(0, SD, path.c_str(), 255, 44100, false);
    
    currentTrack = index;
    isPlaying = true;
    
    delay(50);
    trackDurationMs = synth.getStreamDurationMs(0);
}

void updatePlayerLogic() {
    if (uiState == STATE_PLAYER && isPlaying) {
        if (!synth.isStreamPlaying(0) && trackDurationMs > 0) {
            if (isLooping) {
                playTrack(currentTrack); 
            } else {
                int nextTrk = currentTrack + 1;
                if (nextTrk >= playlist.size()) nextTrk = 0;
                playTrack(nextTrk);
            }
        }
    }
}

// ==========================================================
// == ENTRADAS (ENCODER E BOTÃO)
// ==========================================================
void handleInput() {
    int32_t currentCount = encoder.getCount();
    int delta = currentCount - lastEncoderCount;
    
    if (delta != 0) {
        lastEncoderCount = currentCount;
        int dir = (delta > 0) ? 1 : -1;

        if (uiState == STATE_FILE_LIST) {
            listSelectedIndex += dir;
            if (listSelectedIndex < 0) listSelectedIndex = playlist.size() - 1;
            if (listSelectedIndex >= playlist.size()) listSelectedIndex = 0;
            
        } else if (uiState == STATE_PLAYER) {
            if (!isEditingSlider) {
                playerFocusedCtrl += dir;
                if (playerFocusedCtrl < 0) playerFocusedCtrl = CTRL_MAX - 1;
                if (playerFocusedCtrl >= CTRL_MAX) playerFocusedCtrl = 0;
            } else {
                if (playerFocusedCtrl == CTRL_VOL) {
                    int v = currentVolume + (dir * 10);
                    if (v > 255) v = 255;
                    if (v < 0) v = 0;
                    currentVolume = (uint8_t)v;
                    synth.setMasterVolume(currentVolume);
                } else if (playerFocusedCtrl == CTRL_TIME) {
                    int32_t s = seekTargetMs + (dir * 5000); 
                    if (s < 0) s = 0;
                    if (s > trackDurationMs) s = trackDurationMs;
                    seekTargetMs = s;
                }
            }
        }
    }

    bool reading = digitalRead(ENC_SW);
    if (reading == LOW && buttonState == HIGH && (millis() - lastButtonPress > 200)) {
        buttonState = LOW;
        lastButtonPress = millis();

        if (uiState == STATE_FILE_LIST) {
            playTrack(listSelectedIndex);
            uiState = STATE_PLAYER;
        } 
        else if (uiState == STATE_PLAYER) {
            if (isEditingSlider) {
                isEditingSlider = false;
                if (playerFocusedCtrl == CTRL_TIME) {
                    synth.seekStreamMs(0, seekTargetMs);
                }
            } else {
                switch (playerFocusedCtrl) {
                    case CTRL_BACK:
                        uiState = STATE_FILE_LIST;
                        break;
                    case CTRL_PREV:
                        playTrack(currentTrack > 0 ? currentTrack - 1 : playlist.size() - 1);
                        break;
                    case CTRL_REV5: {
                        uint32_t p = synth.getStreamPositionMs(0);
                        synth.seekStreamMs(0, p > 5000 ? p - 5000 : 0);
                        break;
                    }
                    case CTRL_PLAY:
                        if (isPlaying) { synth.pauseStream(0); isPlaying = false; }
                        else { synth.resumeStream(0); isPlaying = true; }
                        break;
                    case CTRL_FWD5: {
                        uint32_t p = synth.getStreamPositionMs(0);
                        if (p + 5000 < trackDurationMs) synth.seekStreamMs(0, p + 5000);
                        break;
                    }
                    case CTRL_NEXT:
                        playTrack(currentTrack < playlist.size() - 1 ? currentTrack + 1 : 0);
                        break;
                    case CTRL_LOOP:
                        isLooping = !isLooping;
                        break;
                    case CTRL_TIME:
                        isEditingSlider = true;
                        seekTargetMs = synth.getStreamPositionMs(0);
                        break;
                    case CTRL_VOL:
                        isEditingSlider = true;
                        break;
                }
            }
        }
    } else if (reading == HIGH) {
        buttonState = HIGH;
    }
}

// ==========================================================
// == DESENHO DA INTERFACE (OLED)
// ==========================================================
void drawUI() {
    display.clearDisplay();
    
    if (uiState == STATE_FILE_LIST) {
        drawFileList();
    } else {
        drawPlayer();
    }
    
    display.display();
}

// Algoritmo profissional de Marquee (Pausa no início -> Desliza -> Pausa no fim -> Reseta)
int getMarqueeOffset(int textWidth, int visibleWidth) {
    if (textWidth <= visibleWidth) return 0;
    
    int waitTime = 1500; // Tempo parado no inicio e no fim (1.5s)
    int speed = 40;      // ms por pixel (ajusta a velocidade)
    int scrollAmount = textWidth - visibleWidth + 12; // Quanto ele precisa andar (+ margem)
    
    int cycleTime = waitTime + (scrollAmount * speed) + waitTime;
    int t = millis() % cycleTime;
    
    if (t < waitTime) return 0; // Pausado no começo
    else if (t < waitTime + scrollAmount * speed) return (t - waitTime) / speed; // Rolando
    else return scrollAmount; // Pausado no final
}

void drawFileList() {
    // Área Amarela
    display.fillRect(0, 0, 128, 16, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(2, 4);
    display.print("Playlist WAV (");
    display.print(playlist.size());
    display.print(")");

    // Área Azul
    display.setTextColor(SSD1306_WHITE);
    int visibleItems = 4;
    
    if (listSelectedIndex < listScrollOffset) listScrollOffset = listSelectedIndex;
    if (listSelectedIndex >= listScrollOffset + visibleItems) listScrollOffset = listSelectedIndex - visibleItems + 1;

    int yPos = 20;
    for (int i = 0; i < visibleItems; i++) {
        int idx = listScrollOffset + i;
        if (idx >= playlist.size()) break;

        String name = playlist[idx];

        if (idx == listSelectedIndex) {
            display.fillRect(0, yPos - 1, 128, 10, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
            
            int textWidth = name.length() * 6;
            int offset = getMarqueeOffset(textWidth, 124);
            display.setCursor(2 - offset, yPos); 
        } else {
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(2, yPos);
        }

        display.print(name);
        yPos += 11;
    }
}

void formatTime(uint32_t ms, char* buf) {
    uint32_t totSec = ms / 1000;
    uint32_t min = totSec / 60;
    uint32_t sec = totSec % 60;
    sprintf(buf, "%02d:%02d", min, sec);
}

void drawPlayer() {
    // --- Área Amarela (Nome da Música) ---
    display.fillRect(0, 0, 128, 16, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    
    String songName = playlist[currentTrack];
    int textWidth = songName.length() * 6;
    
    if (textWidth > 128) {
        int offset = getMarqueeOffset(textWidth, 128);
        display.setCursor(2 - offset, 4);
    } else {
        display.setCursor((128 - textWidth) / 2, 4); 
    }
    display.print(songName);

    // --- Área Azul (Player) ---
    display.setTextColor(SSD1306_WHITE);

    uint32_t curPos = synth.getStreamPositionMs(0);
    uint32_t curDisplayMs = (isEditingSlider && playerFocusedCtrl == CTRL_TIME) ? seekTargetMs : curPos;

    // Tempo Atual / Total
    char tBuf[16];
    char dBuf[16];
    formatTime(curDisplayMs, tBuf);
    formatTime(trackDurationMs, dBuf);
    display.setCursor(2, 18);
    display.print(tBuf);
    display.print("/");
    display.print(dBuf);

    // Slider Horizontal de Tempo
    int sX = 5, sY = 28, sW = 100, sH = 6;
    display.drawRect(sX, sY, sW, sH, SSD1306_WHITE);
    if (trackDurationMs > 0) {
        int pW = (curDisplayMs * (sW - 2)) / trackDurationMs;
        display.fillRect(sX + 1, sY + 1, pW, sH - 2, SSD1306_WHITE);
    }
    if (playerFocusedCtrl == CTRL_TIME) {
        if (isEditingSlider) display.fillRect(sX - 2, sY - 2, sW + 4, sH + 4, SSD1306_INVERSE);
        else display.drawRect(sX - 2, sY - 2, sW + 4, sH + 4, SSD1306_WHITE);
    }

    // Slider Vertical de Volume
    int vX = 115, vY = 18, vW = 8, vH = 40;
    display.drawRect(vX, vY, vW, vH, SSD1306_WHITE);
    int pV = (currentVolume * (vH - 2)) / 255;
    display.fillRect(vX + 1, vY + vH - 1 - pV, vW - 2, pV, SSD1306_WHITE);
    
    if (playerFocusedCtrl == CTRL_VOL) {
        if (isEditingSlider) display.fillRect(vX - 2, vY - 2, vW + 4, vH + 4, SSD1306_INVERSE);
        else display.drawRect(vX - 2, vY - 2, vW + 4, vH + 4, SSD1306_WHITE);
    }

    // Os 7 Botões (Agora com Voltar incluído!)
    int iconY = 46;
    int spacing = 16;
    int bStartX = 2;

    for (int i = 0; i < 7; i++) {
        int x = bStartX + (i * spacing);
        
        // Verifica estados (Play/Pause: ID 3) | (Loop: ID 6)
        bool state = (i == 3 && isPlaying) || (i == 6 && isLooping);
        drawIcon(x, iconY, i, state);

        if (playerFocusedCtrl == i) {
            display.drawRoundRect(x - 2, iconY - 2, 14, 14, 2, SSD1306_WHITE);
        }
    }
}

// Desenha a Geometria de cada um dos 7 ícones
void drawIcon(int x, int y, int type, bool state) {
    switch (type) {
        case 0: // Ícone Voltar / Lista [☰]
            display.drawLine(x + 1, y + 2, x + 9, y + 2, SSD1306_WHITE);
            display.drawLine(x + 1, y + 5, x + 9, y + 5, SSD1306_WHITE);
            display.drawLine(x + 1, y + 8, x + 9, y + 8, SSD1306_WHITE);
            // Pontinhos da lista
            display.drawPixel(x, y + 2, SSD1306_WHITE);
            display.drawPixel(x, y + 5, SSD1306_WHITE);
            display.drawPixel(x, y + 8, SSD1306_WHITE);
            break;
        case 1: // Prev [|◀]
            display.drawLine(x, y, x, y + 10, SSD1306_WHITE);
            display.fillTriangle(x + 9, y, x + 9, y + 10, x + 2, y + 5, SSD1306_WHITE);
            break;
        case 2: // Rev 5s[◀◀]
            display.fillTriangle(x + 5, y + 2, x + 5, y + 8, x, y + 5, SSD1306_WHITE);
            display.fillTriangle(x + 10, y + 2, x + 10, y + 8, x + 5, y + 5, SSD1306_WHITE);
            break;
        case 3: // Play/Pause
            if (state) { // Pause [||]
                display.fillRect(x + 2, y + 1, 3, 9, SSD1306_WHITE);
                display.fillRect(x + 7, y + 1, 3, 9, SSD1306_WHITE);
            } else { // Play [▶]
                display.fillTriangle(x + 2, y, x + 2, y + 10, x + 10, y + 5, SSD1306_WHITE);
            }
            break;
        case 4: // Fwd 5s[▶▶]
            display.fillTriangle(x, y + 2, x, y + 8, x + 5, y + 5, SSD1306_WHITE);
            display.fillTriangle(x + 5, y + 2, x + 5, y + 8, x + 10, y + 5, SSD1306_WHITE);
            break;
        case 5: // Next [▶|]
            display.fillTriangle(x, y, x, y + 10, x + 8, y + 5, SSD1306_WHITE);
            display.drawLine(x + 10, y, x + 10, y + 10, SSD1306_WHITE);
            break;
        case 6: // Loop
            display.drawCircle(x + 5, y + 5, 4, SSD1306_WHITE);
            if (state) {
                display.drawLine(x + 3, y + 5, x + 5, y + 7, SSD1306_WHITE);
                display.drawLine(x + 5, y + 7, x + 8, y + 3, SSD1306_WHITE);
            }
            break;
    }
}