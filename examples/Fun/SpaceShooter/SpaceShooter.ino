#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ESP32Synth.h" 

// ====================================================================================
// HARDWARE & CONTROL DEFINITIONS
// ====================================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ESP32Synth synth;

#define PIN_BTN_LEFT   13
#define PIN_BTN_RIGHT  12
#define PIN_BTN_FIRE   14
#define PIN_BTN_SHIELD 27

// ====================================================================================
// CINEMA DELAY DSP ENGINE (Integer-Only, IRAM Cache Optimized)
// ====================================================================================
#define DELAY_SIZE 8192
#define DELAY_MASK (DELAY_SIZE - 1)

int32_t delayBuffer[DELAY_SIZE] __attribute__((aligned(16)));
uint32_t delayWriteIdx = 0;

void IRAM_ATTR cinemaDSP(int32_t* mixBuffer, int numSamples) {
    for (int i = 0; i < numSamples; i++) {
        int32_t dry = mixBuffer[i];
        
        // ~125ms delay (eco longo e majestoso)
        uint32_t readIdx = (delayWriteIdx + (DELAY_SIZE - 6000)) & DELAY_MASK;
        int32_t wet = delayBuffer[readIdx];
        
        // Mistura no Master de Saída
        mixBuffer[i] = dry + ((wet * 115) >> 8);
        
        // Realimentação (Feedback) para manter o som flutuando
        delayBuffer[delayWriteIdx] = dry + ((wet * 140) >> 8);
        delayWriteIdx = (delayWriteIdx + 1) & DELAY_MASK;
    }
}

// ====================================================================================
// GAME ENGINE VARIABLES & PHYSICS
// ====================================================================================
int playerX = 64;
int playerVX = 0; // Velocidade de Inércia Linear
const int playerY = 54;
int playerLives = 3;
uint32_t score = 0;

int shieldEnergy = 100;
bool shieldActive = false;
bool shieldOverheated = false; // A nova mecânica de sobrecarga!

struct Laser {
    int x, y;
    bool active;
};
#define MAX_LASERS 4
Laser lasers[MAX_LASERS];
uint32_t lastFireTime = 0;
const uint32_t fireCooldown = 180; 

struct Enemy {
    int x;
    int y_fp;      
    int speed_fp;  
    bool active;
};
#define MAX_ENEMIES 3
Enemy enemies[MAX_ENEMIES];
uint32_t lastEnemySpawn = 0;

struct Star {
    int x, y;
    int speed;
};
#define MAX_STARS 12
Star stars[MAX_STARS];

struct Particle {
    int x, y;
    int vx, vy;
    int life;
};
#define MAX_PARTICLES 15
Particle particles[MAX_PARTICLES];

// ====================================================================================
// CELESTIAL SOUND DESIGN
// ====================================================================================
void triggerLaserSFX() {
    synth.setWave(1, WAVE_SAW); 
    synth.setEnv(1, 1, 90, 0, 50); 
    synth.setTremolo(1, 3200, 160); 
    
    synth.noteOn(1, 120000, 160); 
    synth.slideFreq(1, 120000, 12000, 120); 
}

void triggerExplosionSFX() {
    synth.setWave(2, WAVE_NOISE);
    synth.setEnv(2, 4, 450, 0, 200); 
    synth.setTremolo(2, 1100, 220); 
    synth.noteOn(2, 6000, 230); 
}

void triggerShieldSFX(bool active) {
    if (active) {
        synth.setWave(3, WAVE_TRIANGLE); 
        synth.setEnv(3, 140, 350, 160, 100);
        synth.setVibrato(3, 850, 2800); 
        
        synth.noteOn(3, 12000, 160);
        synth.slideFreq(3, 12000, 35000, 350); 
    } else {
        synth.noteOff(3);
    }
}

void startMusic() {
    synth.setWave(0, WAVE_TRIANGLE); 
    synth.setEnv(0, 20, 0, 60, 450); 
    
    synth.setVibrato(0, 350, 120); 
    
    // Progressão Harmônica Flutuante
    // Cmaj9 -> Amin11 -> Fmaj9 -> Gadd9
    synth.setArpeggio(0, 145, 
                      c3, g3, b3, d4, e4, d4, b3, g3,
                      a2, e3, g3, c4, d4, c4, g3, e3,
                      f2, c3, e3, g3, a3, g3, e3, c3,
                      g2, d3, f3, a3, b3, a3, f3, d3);                  
    synth.noteOn(0, c3, 110); 
}

void setupAudio() {
    if (!synth.begin(4, 15, 2, I2S_32BIT)) {
        Serial.println("Erro ao inicializar ESP32Synth!");
    }
    synth.setMasterVolume(170);
    synth.setCustomDSP(cinemaDSP);
    startMusic();
}

// ====================================================================================
// CORE GAME LOGIC
// ====================================================================================
void spawnExplosion(int x, int y) {
    triggerExplosionSFX();
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particles[i].x = x;
        particles[i].y = y;
        particles[i].vx = random(-4, 5);
        particles[i].vy = random(-4, 5);
        particles[i].life = random(10, 20);
    }
}

void resetGame() {
    playerX = 64;
    playerVX = 0;
    playerLives = 3;
    score = 0;
    
    shieldEnergy = 100;
    shieldActive = false;
    shieldOverheated = false;

    lastFireTime = millis();
    lastEnemySpawn = millis();

    for (int i = 0; i < MAX_LASERS; i++) lasers[i].active = false;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i].life = 0;

    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].x = random(0, SCREEN_WIDTH);
        stars[i].y = random(16, SCREEN_HEIGHT);
        stars[i].speed = random(1, 4);
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
    pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
    pinMode(PIN_BTN_FIRE, INPUT_PULLUP);
    pinMode(PIN_BTN_SHIELD, INPUT_PULLUP);

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        for (;;);
    }
    
    Wire.setClock(400000); // Essencial para rodar a 60 FPS com I2C!

    display.clearDisplay();
    display.display();

    setupAudio();
    resetGame();
}

void loop() {
    static uint32_t lastFrame = millis(); 
    
    display.clearDisplay();

    // ====================================================================================
    // INPUTS & SMOOTH INERTIAL PHYSICS (Corrigido para perfeição)
    // ====================================================================================
    bool btnLeft = (digitalRead(PIN_BTN_LEFT) == LOW);
    bool btnRight = (digitalRead(PIN_BTN_RIGHT) == LOW);
    bool btnFire = (digitalRead(PIN_BTN_FIRE) == LOW);
    bool btnShield = (digitalRead(PIN_BTN_SHIELD) == LOW);

    // Sistema de Aceleração Linear (Deslize natural e responsivo)
    if (btnLeft) {
        playerVX -= 2; 
        if (playerVX < -6) playerVX = -6; 
    } else if (btnRight) {
        playerVX += 2;  
        if (playerVX > 6) playerVX = 6;
    } else {
        // Fricção espacial simétrica: Atrito constante que leva a zero perfeitamente
        if (playerVX > 0) playerVX--;
        else if (playerVX < 0) playerVX++;
    }

    playerX += playerVX;
    if (playerX < 6) { playerX = 6; playerVX = 0; }
    if (playerX > SCREEN_WIDTH - 6) { playerX = SCREEN_WIDTH - 6; playerVX = 0; }

    // ====================================================================================
    // SISTEMA TÁTICO DO ESCUDO (Overheat / Sobrecarga)
    // ====================================================================================
    if (shieldEnergy <= 0) shieldOverheated = true; // Quebrou!
    if (shieldEnergy >= 60) shieldOverheated = false; // Reparos concluídos aos 60%

    if (btnShield && !shieldOverheated) {
        if (!shieldActive) {
            shieldActive = true;
            triggerShieldSFX(true);
        }
        shieldEnergy -= 2; // Drena muito mais rápido agora
        if (shieldEnergy < 0) shieldEnergy = 0;
    } else {
        if (shieldActive) {
            shieldActive = false;
            triggerShieldSFX(false);
        }
        if (shieldEnergy < 100) shieldEnergy += 1; // Recarrega lento
    }

    if (btnFire && (millis() - lastFireTime >= fireCooldown)) {
        for (int i = 0; i < MAX_LASERS; i++) {
            if (!lasers[i].active) {
                lasers[i].x = playerX;
                lasers[i].y = playerY - 4;
                lasers[i].active = true;
                triggerLaserSFX();
                lastFireTime = millis();
                break;
            }
        }
    }

    // ====================================================================================
    // PROCESS GAME WORLD
    // ====================================================================================

    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].y += stars[i].speed;
        if (stars[i].y >= SCREEN_HEIGHT) {
            stars[i].x = random(0, SCREEN_WIDTH);
            stars[i].y = 16;
            stars[i].speed = random(1, 4);
        }
        display.drawPixel(stars[i].x, stars[i].y, SSD1306_WHITE);
    }

    for (int i = 0; i < MAX_LASERS; i++) {
        if (lasers[i].active) {
            lasers[i].y -= 4; 
            if (lasers[i].y < 16) {
                lasers[i].active = false;
            } else {
                display.drawFastVLine(lasers[i].x, lasers[i].y, 5, SSD1306_WHITE);
            }
        }
    }

    if (millis() - lastEnemySpawn > 2200) {
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].active) {
                enemies[i].x = random(10, SCREEN_WIDTH - 10);
                enemies[i].y_fp = 16 << 8; 
                enemies[i].speed_fp = random(90, 160); 
                enemies[i].active = true;
                lastEnemySpawn = millis();
                break;
            }
        }
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            enemies[i].y_fp += enemies[i].speed_fp;
            int enemyY = enemies[i].y_fp >> 8; 
            
            display.drawRect(enemies[i].x - 3, enemyY - 2, 7, 5, SSD1306_WHITE);
            display.drawPixel(enemies[i].x - 5, enemyY, SSD1306_WHITE);
            display.drawPixel(enemies[i].x + 5, enemyY, SSD1306_WHITE);

            if (enemyY >= SCREEN_HEIGHT) {
                enemies[i].active = false;
                playerLives--;
                triggerExplosionSFX();
            }

            for (int l = 0; l < MAX_LASERS; l++) {
                if (lasers[l].active && enemies[i].active) {
                    if (abs(lasers[l].x - enemies[i].x) < 5 && abs(lasers[l].y - enemyY) < 5) {
                        lasers[l].active = false;
                        enemies[i].active = false;
                        score += 20;
                        spawnExplosion(enemies[i].x, enemyY);
                    }
                }
            }

            if (enemies[i].active && abs(enemies[i].x - playerX) < 7 && abs(enemyY - playerY) < 6) {
                enemies[i].active = false;
                spawnExplosion(enemies[i].x, enemyY);
                if (shieldActive) {
                    shieldEnergy = (shieldEnergy > 35) ? shieldEnergy - 35 : 0;
                } else {
                    playerLives--;
                }
            }
        }
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0) {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].life--;
            if (particles[i].y >= 16 && particles[i].y < SCREEN_HEIGHT) {
                display.drawPixel(particles[i].x, particles[i].y, SSD1306_WHITE);
            }
        }
    }

    // ====================================================================================
    // RENDER PLAYER & SHIELD
    // ====================================================================================
    display.drawLine(playerX, playerY - 4, playerX - 5, playerY + 2, SSD1306_WHITE);
    display.drawLine(playerX, playerY - 4, playerX + 5, playerY + 2, SSD1306_WHITE);
    display.drawLine(playerX - 5, playerY + 2, playerX + 5, playerY + 2, SSD1306_WHITE);
    display.drawPixel(playerX, playerY - 1, SSD1306_WHITE);

    if (shieldActive) {
        display.drawCircle(playerX, playerY, 9, SSD1306_WHITE);
    }

    // ====================================================================================
    // OLED YELLOW ZONE (HUD - Y: 0 to 15)
    // ====================================================================================
    display.drawFastHLine(0, 15, SCREEN_WIDTH, SSD1306_WHITE);

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(2, 4);
    display.print(F("SCORE:"));
    display.print(score);

    display.setCursor(55, 4);
    
    // Feedback Visual do Overheat: O texto pisca "OVR!" quando está sobrecarregado
    if (shieldOverheated && (millis() % 200 > 100)) {
        display.print(F("OVR!"));
    } else {
        display.print(F("S:"));
    }
    
    display.drawRect(67, 4, 24, 7, SSD1306_WHITE);
    int barWidth = (shieldEnergy * 22) / 100;
    display.fillRect(68, 5, barWidth, 5, SSD1306_WHITE);

    for (int i = 0; i < playerLives; i++) {
        int lx = 104 + (i * 8);
        int ly = 7;
        display.drawTriangle(lx, ly - 3, lx - 3, ly + 3, lx + 3, ly + 3, SSD1306_WHITE);
    }

    // ====================================================================================
    // GAME OVER SCREEN
    // ====================================================================================
    if (playerLives <= 0) {
        synth.noteOff(0); 
        triggerExplosionSFX();
        delay(200);
        triggerExplosionSFX();
        
        while (true) {
            display.clearDisplay();
            display.setTextSize(2);
            display.setCursor(10, 10);
            display.print(F("GAME OVER"));
            display.setTextSize(1);
            display.setCursor(18, 36);
            display.print(F("FINAL SCORE: "));
            display.print(score);
            display.setCursor(10, 52);
            display.print(F("Pressione ATIRAR"));
            display.display();

            if (digitalRead(PIN_BTN_FIRE) == LOW) {
                resetGame();
                startMusic(); 
                lastFrame = millis(); 
                break;
            }
            delay(50);
        }
    }

    display.display();
    
    // Sistema Delta Time 60 FPS cravado
    uint32_t now = millis();
    int32_t waitTime = 16 - (now - lastFrame);
    if (waitTime > 0) {
        delay(waitTime);
    }
    lastFrame = millis();
}