/**
 * @file JSBach_Toccata_Stereo_FX.ino
 * @author Danilo Gabriel
 * @brief Plays J.S. Bach's Toccata and Fugue in D minor, BWV 565,
 *        with dynamic stereo choreography, modular buses, built-in FX,
 *        LFO modulation, and anti-aliased PolyBLEP oscillators.
 */
#include "ESP32Synth.h"
#include "ESP32Synth_Patches.hpp"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
const int BCK_PIN  = 4;
const int WS_PIN   = 6;
const int DATA_PIN = 5;
#else
const int BCK_PIN  = 4;
const int WS_PIN   = 15;
const int DATA_PIN = 2;
#endif

ESP32Synth synth;

volatile uint8_t counter = 0;

void delay_ms(int ms) {
  delay(ms);
}

uint8_t boost(int v) {
  int val = v * 2;
  return (val > 255) ? 255 : val;
}

void wave01_volume(int v) { synth.setVolume(0, boost(v)); }
void wave02_volume(int v) { synth.setVolume(1, boost(v)); }
void wave03_volume(int v) { synth.setVolume(2, boost(v)); }
void wave04_volume(int v) { synth.setVolume(3, boost(v)); }
void wave05_volume(int v) { synth.setVolume(4, boost(v)); }
void wave06_volume(int v) { synth.setVolume(5, boost(v)); }
void wave07_volume(int v) { synth.setVolume(6, boost(v)); }
void wave08_volume(int v) { synth.setVolume(7, boost(v)); }
void wave09_volume(int v) { synth.setVolume(8, v); }
void wave10_volume(int v) { synth.setVolume(9, v); }
void wave11_volume(int v) { synth.setVolume(10, v); }

void wave01_frequency(int f) { synth.setFrequency(0, f); }
void wave02_frequency(int f) { synth.setFrequency(1, f); }
void wave03_frequency(int f) { synth.setFrequency(2, f); }
void wave04_frequency(int f) { synth.setFrequency(3, f); }
void wave05_frequency(int f) { synth.setFrequency(4, f); }
void wave06_frequency(int f) { synth.setFrequency(5, f); }
void wave07_frequency(int f) { synth.setFrequency(6, f); }
void wave08_frequency(int f) { synth.setFrequency(7, f); }
void wave09_frequency(int f) { synth.setFrequency(8, f); }
void wave10_frequency(int f) { synth.setFrequency(9, f); }
void wave11_frequency(int f) { synth.setFrequency(10, f); }

void stop01() { wave01_volume(0); }
void stop02() { wave02_volume(0); }
void stop03() { wave03_volume(0); }
void stop04() { wave04_volume(0); }
void stop05() { wave05_volume(0); }
void stop06() { wave06_volume(0); }
void stop07() { wave07_volume(0); }
void stop08() { wave08_volume(0); }
void stop09() { wave09_volume(0); }
void stop10() { wave10_volume(0); }
void stop11() { wave11_volume(0); }

void voice01(unsigned int a) {
  wave09_frequency(a);
  wave01_frequency(a * 2);
}
void voice01v() {
  wave01_volume(85);
  wave09_volume(160);
}
void voice01s() {
  stop01();
  stop09();
}

void voice02(unsigned int a) {
  wave09_frequency(a);
  wave10_frequency((int)(a * 0.997));
}
void voice02v() {
  synth.setPan(8, -85);
  synth.setPan(9, 85);
  wave09_volume(127);
  wave10_volume(127);
}
void voice02s() {
  stop09();
  stop10();
  synth.setPan(8, 0);
  synth.setPan(9, 34);
}

void voice03(unsigned int a) {
  if (counter == 0) wave09_frequency(a / 2);
  else wave01_frequency(a);
}

void voice04(unsigned int a) {
  uint8_t i = counter;
  unsigned int f = a;
  while (i != 0) {
    f /= 2;
    i--;
  }
  wave01_frequency(f);
}

void resetDynamicPatchState() {
  const int8_t  manualPan[8] = { -8, 48, -56, 76, -84, 100, -104, 116 };
  const uint8_t manualPW[8]  = { 112, 120, 126, 128, 122, 114, 124, 108 };

  for (int i = 0; i <= 7; i++) {
    synth.setPan(i, manualPan[i]);
    synth.setPulseWidth(i, manualPW[i]);
    synth.setVibrato(i, 0, 0);
    synth.setTremolo(i, 0, 0);
    synth.setAutoPan(i, 0, 0);
  }

  synth.setPan(8, 0);
  synth.setPan(9, 34);
  synth.setPan(10, 0);
  synth.setAutoPan(10, 220, 85);
  synth.setVibrato(10, 520, 60);

  synth.setBusFilter(2, 0, 3600, 85, FX_FILTER_LPF, 255);
  synth.setBusDelay(3, 1, 240, 95, 150, true, 95);
  synth.setBusReverb(0, 0, 200, 125, 255, 96);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("ESP32Synth - J.S. Bach's Toccata in D minor (Dynamic Stereo Cathedral)");

  if (!synth.begin(BCK_PIN, WS_PIN, DATA_PIN, I2S_32BIT)) {
    Serial.println("ERROR: Failed to initialize synthesizer.");
    while (1) delay(1000);
  }

  Serial.printf("Chip: %s | Sample Rate: %d Hz\n", synth.getChipModel(), (int)synth.getSampleRate());

  synth.setMasterVolume(215);
  synth.setPanLaw(PAN_LAW_CONSTANT_POWER);
  synth.setMasterStereoWidth(284);

  // Bus 1: Great/Swell Organ Manuals (Voices 0..7)
  for (int i = 0; i <= 7; i++) {
    synth.setCustomWave(i, ESP32Patches::AA_Pulse);
    synth.setStartPhase(i, i * 45);
    synth.setVoiceBus(i, 1);
  }

  // Bus 2: Pedalboard & Principal 8' (Voices 8..9)
  synth.setCustomWave(8, ESP32Patches::AA_Saw);
  synth.setVoiceBus(8, 2);

  synth.setCustomWave(9, ESP32Patches::AA_Saw);
  synth.setStartPhase(9, 135);
  synth.setVoiceBus(9, 2);

  // Bus 3: Positiv Flute / Solo Arpeggios (Voice 10)
  synth.setWave(10, WAVE_TRIANGLE);
  synth.setVoiceBus(10, 3);

  for (int i = 0; i <= 10; i++) {
    synth.setEnv(i, 0, 0, 155, 60000);
    synth.noteOn(i, 44000, 0);
  }

  // Bus 1 FX: Stereo Chorus + Swell LPF + 24Hz DC-Blocking HPF
  synth.setBusChorus(1, 0, 55, 85, 14, 15, 95, 180);
  synth.setBusFilter(1, 1, 6000, 72, FX_FILTER_LPF, 255);
  synth.setBusFilter(1, 2, 24, 71, FX_FILTER_HPF, 255);
  synth.setBusMix(1, 235);

  // Bus 2 FX: Warm Sub-Bass LPF + Tight Low-End Width
  synth.setBusFilter(2, 0, 3600, 85, FX_FILTER_LPF, 255);
  synth.setBusWidth(2, 180);
  synth.setBusMix(2, 235);

  // Bus 3 FX: Chorus + Tempo-Locked Stereo Ping-Pong Delay
  synth.setBusChorus(3, 0, 75, 95, 11, 15, 80, 180);
  synth.setBusDelay(3, 1, 240, 95, 150, true, 95);
  synth.setBusMix(3, 225);

  // Bus 0 Master FX (Post-Mixdown): Cathedral Reverb + Master Bus Glue
  synth.setBusReverb(0, 0, 200, 125, 255, 96);
  synth.setBusCompressor(0, 1, 165, 3, 8, 140, 16, 220);

  resetDynamicPatchState();

  // Modular LFOs for pipe air, ensemble stereo drift, and swell expression
  synth.setLFO(0, LFO_SINE, 22, 0, 0, 500, false);
  synth.routeLFOToVoice(0, LFO_TARGET_PULSE_WIDTH, 0, 7, 16, 0, false);

  synth.setLFO(1, LFO_TRIANGLE, 16, 90, 0, 500, false);
  synth.routeLFOToVoice(1, LFO_TARGET_VOICE_PAN, 1, 7, 12, 0, false);

  synth.setLFO(2, LFO_SINE, 18, 0, 0, 500, false);
  synth.assignLFOToBusFX(2, 1, 1, FX_FILTER_CUTOFF, 4600, 6800);

  Serial.println("Synth initialized. Starting playback...");
}

void loop() {
  resetDynamicPatchState();

  // --- Opening Statement 1: Wide contrary stereo sweep ---
  synth.slidePan(0, -45, 0, 1600);
  synth.slidePan(8, 45, 0, 1600);
  voice01v();
  voice01(a4);
  delay_ms(100);
  voice01(g4);
  delay_ms(100);
  voice01(a4);
  delay_ms(1500);
  voice01s();
  delay_ms(1500);

  synth.slidePan(0, -65, 65, 1300);
  synth.slidePan(8, 65, -65, 1300);
  voice01v();
  voice01(g4);
  delay_ms(225);
  voice01(f4);
  delay_ms(150);
  voice01(e4);
  delay_ms(135);
  voice01(d4);
  delay_ms(125);
  voice01(cs4);
  delay_ms(650);
  voice01(d4);
  delay_ms(1500);
  voice01s();
  delay_ms(2000);

  // --- Opening Statement 2: Celeste stereo answer converging inward ---
  voice02v();
  voice02(a3);
  delay_ms(100);
  voice02(g3);
  delay_ms(100);
  voice02(a3);
  delay_ms(1500);
  voice02s();
  delay_ms(1000);

  voice02v();
  synth.slidePan(8, -85, -22, 1600);
  synth.slidePan(9, 85, 22, 1600);
  voice02(e3);
  delay_ms(400);
  voice02(f3);
  delay_ms(350);
  voice02(cs3);
  delay_ms(450);
  voice02(d3);
  delay_ms(1500);
  voice02s();
  delay_ms(2000);

  // --- Opening Statement 3: Deep pedal octave sweep landing dead-center ---
  synth.slidePan(0, 75, 0, 1600);
  synth.slidePan(8, -75, 0, 1600);
  voice01v();
  voice01(a2);
  delay_ms(100);
  voice01(g2);
  delay_ms(100);
  voice01(a2);
  delay_ms(1500);
  voice01s();
  delay_ms(1500);

  synth.slidePan(0, -50, 0, 1400);
  synth.slidePan(8, 50, 0, 1400);
  voice01v();
  voice01(g2);
  delay_ms(155);
  voice01(f2);
  delay_ms(140);
  voice01(e2);
  delay_ms(130);
  voice01(d2);
  delay_ms(125);
  voice01(cs2);
  delay_ms(650);
  voice01(d2);
  delay_ms(1500);
  voice01s();
  delay_ms(2000);

  // --- Section 2: Pedal D1 + Expanding Stereo Diminished Chord ---
  synth.setPan(8, 0);
  synth.setPan(0, -20);
  synth.setPan(1, 20);
  synth.setPan(2, -52);
  synth.setPan(3, 52);
  synth.setPan(4, -86);
  synth.setPan(5, 86);

  wave09_volume(255);
  wave09_frequency(d1);
  delay_ms(2000);
  wave09_volume(191);
  wave01_volume(31);
  wave01_frequency(cs3);
  delay_ms(750);
  wave09_volume(151);
  wave01_volume(25);
  wave02_volume(25);
  wave02_frequency(e3);
  delay_ms(700);
  wave09_volume(123);
  wave01_volume(21);
  wave02_volume(21);
  wave03_volume(21);
  wave03_frequency(g3);
  delay_ms(650);
  wave09_volume(103);
  wave01_volume(18);
  wave02_volume(18);
  wave03_volume(18);
  wave04_volume(18);
  wave04_frequency(as3);
  delay_ms(800);
  wave09_volume(95);
  wave01_volume(15);
  wave02_volume(15);
  wave03_volume(15);
  wave04_volume(15);
  wave05_volume(15);
  wave05_frequency(cs4);
  delay_ms(1000);
  wave09_volume(75);
  wave01_volume(14);
  wave02_volume(14);
  wave03_volume(14);
  wave04_volume(14);
  wave05_volume(14);
  wave06_volume(14);
  wave06_frequency(e4);
  delay_ms(1500);

  synth.slidePanTo(0, -14, 2000);
  synth.slidePanTo(2, -45, 2000);
  synth.slidePanTo(3, 45, 2000);
  synth.slidePanTo(4, -75, 2000);

  wave09_volume(121);
  wave01_volume(14);
  stop02();
  wave03_volume(20);
  wave04_volume(14);
  wave05_volume(14);
  stop06();
  wave01_frequency(d3);
  wave04_frequency(a3);
  wave05_frequency(d4);
  delay_ms(1000);
  wave03_frequency(e3);
  delay_ms(1500);
  wave03_frequency(fs3);
  delay_ms(3000);
  stop01();
  stop03();
  stop04();
  stop05();
  stop09();
  delay_ms(2000);

  // --- Section 3: Call & Response Stereo Balconies ---
  wave09_volume(150);
  counter = 0;
  while (counter != 2) {
    if (counter == 0) {
      synth.setPan(8, -68);
    } else {
      synth.setPan(0, 68);
      synth.setAutoPan(0, 240, 28);
      synth.setTremolo(0, 580, 28);
    }
    voice03(cs5);
    delay_ms(300);
    voice03(d5);
    delay_ms(220);
    voice03(e5);
    delay_ms(150);
    voice03(cs5);
    delay_ms(130);
    voice03(d5);
    delay_ms(120);
    voice03(e5);
    delay_ms(120);
    voice03(cs5);
    delay_ms(120);
    voice03(d5);
    delay_ms(120);
    voice03(e5);
    delay_ms(130);
    voice03(cs5);
    delay_ms(150);
    voice03(d5);
    delay_ms(220);
    voice03(e5);
    delay_ms(300);
    voice03(f5);
    delay_ms(220);
    voice03(g5);
    delay_ms(150);
    voice03(e5);
    delay_ms(130);
    voice03(f5);
    delay_ms(120);
    voice03(g5);
    delay_ms(120);
    voice03(e5);
    delay_ms(120);
    voice03(f5);
    delay_ms(120);
    voice03(g5);
    delay_ms(130);
    voice03(e5);
    delay_ms(150);
    voice03(f5);
    delay_ms(220);
    voice03(g5);
    delay_ms(300);
    voice03(a5);
    delay_ms(220);
    voice03(as5);
    delay_ms(150);
    voice03(g5);
    delay_ms(130);
    voice03(a5);
    delay_ms(120);
    voice03(as5);
    delay_ms(120);
    voice03(g5);
    delay_ms(120);
    voice03(a5);
    delay_ms(140);
    voice03(as5);
    delay_ms(200);
    voice03(g5);
    delay_ms(300);
    if (counter == 0) synth.slidePanTo(8, 0, 1000);
    else synth.slidePanTo(0, 0, 1000);
    voice03(a5);
    delay_ms(1000);
    voice01s();
    delay_ms(2000);
    counter++;
    wave01_volume(75);
  }
  synth.setAutoPan(0, 0, 0);
  synth.setTremolo(0, 0, 0);

  // --- Section 4: Wide Contrary Stereo Cascade ---
  synth.slidePan(0, -95, 95, 6500);
  synth.slidePan(8, 95, -95, 6500);
  voice01v();
  voice01(a4);
  delay_ms(350);
  voice01(g4);
  delay_ms(250);
  voice01(as4);
  delay_ms(200);
  voice01(e4);
  delay_ms(170);
  voice01(g4);
  delay_ms(150);
  voice01(as4);
  delay_ms(135);
  voice01(e4);
  delay_ms(125);
  voice01(f4);
  delay_ms(120);
  voice01(a4);
  delay_ms(115);
  voice01(d4);
  delay_ms(110);
  voice01(f4);
  delay_ms(110);
  voice01(a4);
  delay_ms(110);
  voice01(d4);
  delay_ms(110);
  voice01(e4);
  delay_ms(110);
  voice01(g4);
  delay_ms(110);
  voice01(c4);
  delay_ms(110);
  voice01(e4);
  delay_ms(110);
  voice01(g4);
  delay_ms(110);
  voice01(c4);
  delay_ms(110);
  voice01(d4);
  delay_ms(110);
  voice01(f4);
  delay_ms(110);
  voice01(as3);
  delay_ms(110);
  voice01(d4);
  delay_ms(110);
  voice01(f4);
  delay_ms(110);
  voice01(as3);
  delay_ms(110);
  voice01(c4);
  delay_ms(110);
  voice01(e4);
  delay_ms(110);
  voice01(a3);
  delay_ms(110);
  voice01(c4);
  delay_ms(110);
  voice01(e4);
  delay_ms(110);
  voice01(a3);
  delay_ms(110);
  voice01(as3);
  delay_ms(110);
  voice01(d4);
  delay_ms(110);
  voice01(g3);
  delay_ms(110);
  voice01(as3);
  delay_ms(110);
  voice01(d4);
  delay_ms(110);
  voice01(g3);
  delay_ms(110);
  voice01(a3);
  delay_ms(110);
  voice01(c4);
  delay_ms(110);
  voice01(f3);
  delay_ms(110);
  voice01(a3);
  delay_ms(110);
  voice01(c4);
  delay_ms(110);
  voice01(f3);
  delay_ms(110);
  voice01(g3);
  delay_ms(110);
  voice01(as3);
  delay_ms(110);
  voice01(e3);
  delay_ms(110);
  voice01(g3);
  delay_ms(110);
  voice01(as3);
  delay_ms(110);
  voice01(e3);
  delay_ms(110);
  voice01(f3);
  delay_ms(110);
  voice01(a3);
  delay_ms(110);
  voice01(d3);
  delay_ms(110);
  voice01(f3);
  delay_ms(110);
  voice01(a3);
  delay_ms(110);
  voice01(d3);
  delay_ms(115);
  voice01(e3);
  delay_ms(125);
  voice01(g3);
  delay_ms(135);
  voice01(cs3);
  delay_ms(150);
  voice01(e3);
  delay_ms(170);
  voice01(g3);
  delay_ms(200);
  voice01(cs3);
  delay_ms(700);
  voice01s();

  // --- Section 5: D1 Pedal Swell + 8-Voice Stereo Cathedral Wall ---
  synth.setPan(8, 0);
  wave09_frequency(d1);
  wave09_volume(255);
  delay_ms(1500);
  counter = 255;
  while (counter > 62) {
    wave09_volume(counter);
    counter--;
    delay_ms(1);
  }

  const int8_t dimWallPan[8] = { -112, 112, -82, 82, -52, 52, -22, 22 };
  for (int i = 0; i <= 7; i++) synth.setPan(i, dimWallPan[i]);

  wave01_volume(11);
  wave02_volume(11);
  wave03_volume(11);
  wave04_volume(11);
  wave05_volume(11);
  wave06_volume(11);
  wave07_volume(11);
  wave08_volume(11);
  wave01_frequency(as4);
  wave02_frequency(g4);
  wave03_frequency(e4);
  wave04_frequency(cs4);
  wave05_frequency(as3);
  wave06_frequency(g3);
  wave07_frequency(e3);
  wave08_frequency(cs3);
  delay_ms(3000);

  stop02();
  stop03();
  stop04();
  stop05();
  stop06();
  stop07();
  stop08();
  stop09();

  synth.slidePanTo(0, 0, 1200);
  synth.setVibrato(0, 520, 85);
  synth.setTremolo(0, 480, 24);

  counter = 11;
  while (counter < 75) {
    counter++;
    wave01_volume(counter);
    delay_ms(1);
  }
  delay_ms(1200);

  wave01_frequency(a4);
  delay_ms(400);
  wave01_frequency(g4);
  delay_ms(250);
  wave01_frequency(f4);
  delay_ms(200);
  wave01_frequency(e4);
  delay_ms(200);
  wave01_frequency(d4);
  delay_ms(200);
  wave01_frequency(cs4);
  delay_ms(200);
  wave01_frequency(b3);
  delay_ms(220);
  wave01_frequency(cs4);
  delay_ms(250);
  wave01_frequency(a3);
  delay_ms(250);
  wave01_frequency(cs4);
  delay_ms(220);
  wave01_frequency(e4);
  delay_ms(200);
  wave01_frequency(g4);
  delay_ms(300);
  wave01_frequency(f4);
  delay_ms(200);

  counter = 0;
  while (counter < 9) {
    if (counter & 1) wave01_frequency(f4);
    else wave01_frequency(g4);
    counter++;
    delay_ms(80);
  }
  wave01_frequency(f4);
  delay_ms(300);
  wave01_frequency(e4);
  delay_ms(600);

  synth.setVibrato(0, 0, 0);
  synth.setTremolo(0, 0, 0);

  counter = 75;
  while (counter > 23) {
    counter--;
    wave01_volume(counter);
    delay_ms(1);
  }

  synth.setPan(0, -18);
  synth.setPan(1, 80);
  synth.setPan(2, -80);
  synth.setPan(3, 45);
  synth.setPan(4, -45);

  wave02_volume(13);
  wave03_volume(13);
  wave04_volume(13);
  wave05_volume(13);
  wave09_volume(95);
  wave09_frequency(d1);
  wave01_frequency(f4);
  wave02_frequency(d4);
  wave03_frequency(a3);
  wave04_frequency(f3);
  wave05_frequency(d3);
  delay_ms(3000);

  stop01();
  stop02();
  stop03();
  stop04();
  stop05();
  stop09();
  delay_ms(1000);

  // --- Section 6: Pedal-Point A4 Ostinato sweeping across stereo ---
  synth.slidePan(0, -60, 60, 2600);
  wave01_volume(75);
  wave01_frequency(a4);
  delay_ms(200);
  counter = 0;
  while (counter != 2) {
    wave01_frequency(d5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    wave01_frequency(e5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    wave01_frequency(f5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    counter++;
  }
  wave01_frequency(g5);
  delay_ms(120);
  wave01_frequency(a4);
  delay_ms(120);
  wave01_frequency(e5);
  delay_ms(120);

  synth.slidePan(0, 60, -60, 1500);
  counter = 0;
  while (counter != 2) {
    wave01_frequency(a4);
    delay_ms(120);
    wave01_frequency(f5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    wave01_frequency(g5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    wave01_frequency(a5);
    delay_ms(120);
    counter++;
  }

  counter = 0;
  goto skip1;
  while (counter != 3) {
    if (counter == 1) synth.slidePan(0, -70, 70, 3200);
    else if (counter == 2) synth.slidePan(0, 70, 0, 3200);
    wave01_frequency(a4);
    delay_ms(120);
    voice04(a5);
    delay_ms(120);
skip1:
    wave01_frequency(a4);
    delay_ms(120);
    voice04(as5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    voice04(g5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    voice04(a5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    voice04(f5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    voice04(g5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    voice04(e5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    voice04(f5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    voice04(d5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);

    if (counter != 0) {
      voice04(g5);
      delay_ms(120);
      wave01_frequency(a4);
      delay_ms(120);
      voice04(e5);
      delay_ms(120);
      wave01_frequency(a4);
      delay_ms(120);
      voice04(f5);
      delay_ms(120);
      wave01_frequency(a4);
      delay_ms(120);
      voice04(d5);
      delay_ms(120);
      wave01_frequency(a4);
      delay_ms(120);
    }
    voice04(e5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    voice04(cs5);
    delay_ms(120);
    wave01_frequency(a4);
    delay_ms(120);
    voice04(d5);
    delay_ms(120);
    counter++;
  }
  delay_ms(390);
  stop01();
  delay_ms(390);

  // --- Section 7: Baroque Positiv Arpeggios (Ping-Pong Delay) & Cadence Chords ---
  synth.setPan(0, -32);
  synth.setPan(1, 68);
  synth.setPan(2, -76);
  synth.setPan(3, 32);

  counter = 0;
  while (counter != 2) {
    wave11_volume(255);
    wave11_frequency(d4);
    delay_ms(120);
    wave11_frequency(f4);
    delay_ms(120);
    wave11_frequency(as4);
    delay_ms(120);
    wave11_frequency(f4);
    delay_ms(120);
    wave11_frequency(c4);
    delay_ms(120);
    wave11_frequency(e4);
    delay_ms(120);
    wave11_frequency(a4);
    delay_ms(120);
    wave11_frequency(e4);
    delay_ms(120);
    wave11_frequency(as3);
    delay_ms(120);
    wave11_frequency(d4);
    delay_ms(120);
    wave11_frequency(g4);
    delay_ms(120);
    wave11_frequency(d4);
    delay_ms(120);
    wave11_frequency(a3);
    delay_ms(120);
    wave11_frequency(cs4);
    delay_ms(120);
    wave11_frequency(e4);
    delay_ms(120);
    wave11_frequency(a4);
    delay_ms(150);

    stop11();
    wave09_volume(103);
    wave01_volume(18);
    wave09_frequency(d2);
    wave01_frequency(d4);
    delay_ms(270);
    wave02_volume(18);
    wave03_volume(18);
    wave02_frequency(f4);
    wave03_frequency(as4);
    delay_ms(300);
    stop02();
    stop03();
    wave09_frequency(c2);
    wave01_frequency(a3);
    delay_ms(315);
    wave02_volume(18);
    wave03_volume(18);
    wave02_frequency(e4);
    wave03_frequency(a4);
    delay_ms(340);
    stop02();
    stop03();
    wave09_frequency(as1);
    wave01_frequency(as3);
    delay_ms(350);
    wave02_volume(18);
    wave03_volume(18);
    wave02_frequency(d4);
    wave03_frequency(g4);
    delay_ms(400);
    wave04_volume(18);
    wave09_frequency(a1);
    wave01_frequency(a4);
    wave02_frequency(e4);
    wave03_frequency(cs4);
    wave04_frequency(e3);
    delay_ms(1000);
    if (counter != 1) stop01();
    stop02();
    stop03();
    stop04();
    stop09();
    counter++;
  }

  // --- Section 8: Ascending 3-Octave Run sweeping Left -> Right ---
  synth.slidePan(0, -90, 90, 4200);
  counter = 18;
  while (counter < 75) {
    counter++;
    wave01_volume(counter);
    delay_ms(1);
  }
  delay_ms(600);

  wave01_frequency(g4);
  delay_ms(250);
  wave01_frequency(f4);
  delay_ms(200);
  wave01_frequency(e4);
  delay_ms(150);
  wave01_frequency(d4);
  delay_ms(120);
  wave01_frequency(cs4);
  delay_ms(110);
  wave01_frequency(b3);
  delay_ms(120);
  wave01_frequency(cs4);
  delay_ms(150);
  wave01_frequency(a3);
  delay_ms(130);
  wave01_frequency(b3);
  delay_ms(120);
  wave01_frequency(cs4);
  delay_ms(110);
  wave01_frequency(d4);
  delay_ms(110);
  wave01_frequency(e4);
  delay_ms(110);
  wave01_frequency(f4);
  delay_ms(110);
  wave01_frequency(g4);
  delay_ms(110);
  wave01_frequency(a4);
  delay_ms(110);
  wave01_frequency(g4);
  delay_ms(110);
  wave01_frequency(f4);
  delay_ms(110);
  wave01_frequency(e4);
  delay_ms(110);
  wave01_frequency(f4);
  delay_ms(110);
  wave01_frequency(d4);
  delay_ms(110);
  wave01_frequency(f4);
  delay_ms(110);
  wave01_frequency(a4);
  delay_ms(110);
  wave01_frequency(cs5);
  delay_ms(110);
  wave01_frequency(d5);
  delay_ms(200);
  wave01_frequency(a4);
  delay_ms(150);
  wave01_frequency(b4);
  delay_ms(120);
  wave01_frequency(cs5);
  delay_ms(110);
  wave01_frequency(d5);
  delay_ms(110);
  wave01_frequency(e5);
  delay_ms(110);
  wave01_frequency(f5);
  delay_ms(110);
  wave01_frequency(g5);
  delay_ms(110);
  wave01_frequency(a5);
  delay_ms(110);
  wave01_frequency(as5);
  delay_ms(500);
  stop01();
  delay_ms(500);

  // High-octave Positiv break with tighter 180ms Ping-Pong Delay
  synth.setBusDelay(3, 1, 180, 105, 130, true, 105);
  synth.setPan(0, -100);
  synth.setPan(1, 100);
  synth.setPan(2, -48);
  synth.setPan(3, 48);

  counter = 0;
  goto skip2;
  while (counter != 2) {
    stop01();
    stop02();
    stop03();
    stop04();
    stop09();
    wave11_volume(255);
    wave11_frequency(d5);
    delay_ms(120);
    wave11_frequency(f5);
    delay_ms(120);
    wave11_frequency(as5);
    delay_ms(120);
    wave11_frequency(f5);
    delay_ms(120);
    wave11_frequency(c5);
    delay_ms(120);
    wave11_frequency(e5);
    delay_ms(120);
    wave11_frequency(a5);
    delay_ms(120);
    wave11_frequency(e5);
    delay_ms(120);
    wave11_frequency(as4);
    delay_ms(120);
    wave11_frequency(d5);
    delay_ms(120);
    wave11_frequency(g5);
    delay_ms(120);
    wave11_frequency(d5);
    delay_ms(120);
    wave11_frequency(a4);
    delay_ms(120);
    wave11_frequency(cs5);
    delay_ms(120);
    wave11_frequency(e5);
    delay_ms(120);
    wave11_frequency(a5);
    delay_ms(150);
    stop11();

skip2:
    wave09_volume(103);
    wave01_volume(18);
    wave09_frequency(d2);
    wave01_frequency(d5);
    delay_ms(270);
    wave02_volume(18);
    wave03_volume(18);
    wave02_frequency(f5);
    wave03_frequency(as5);
    delay_ms(300);
    stop02();
    stop03();
    wave09_frequency(c2);
    wave01_frequency(a4);
    delay_ms(315);
    wave02_volume(18);
    wave03_volume(18);
    wave02_frequency(e5);
    wave03_frequency(a5);
    delay_ms(340);
    stop02();
    stop03();
    wave09_frequency(as1);
    wave01_frequency(as4);
    delay_ms(350);
    wave02_volume(18);
    wave03_volume(18);
    wave02_frequency(d5);
    wave03_frequency(g5);
    delay_ms(400);
    wave04_volume(18);
    wave09_frequency(a1);
    wave01_frequency(e4);
    wave02_frequency(cs5);
    wave03_frequency(e5);
    wave04_frequency(a5);
    delay_ms(1000);
    counter++;
  }

  synth.setBusDelay(3, 1, 240, 95, 150, true, 95);
  synth.setBusReverb(0, 0, 212, 118, 255, 110);

  wave09_frequency(gs1);
  wave01_frequency(b4);
  wave02_frequency(f4);
  wave03_frequency(d4);
  wave04_frequency(b3);
  delay_ms(1000);

  wave09_frequency(g1);
  wave01_frequency(cs5);
  wave02_frequency(a4);
  wave03_frequency(e4);
  wave04_frequency(a3);
  delay_ms(1500);

  stop02();
  stop03();
  stop04();
  stop09();
  synth.slidePanTo(0, 0, 1500);
  synth.setVibrato(0, 540, 90);
  wave01_frequency(b4);
  counter = 18;
  while (counter < 75) {
    counter++;
    wave01_volume(counter);
    delay_ms(1);
  }
  delay_ms(400);

  wave01_frequency(a4);
  delay_ms(300);
  wave01_frequency(cs5);
  delay_ms(200);
  wave01_frequency(e5);
  delay_ms(200);
  wave01_frequency(g5);
  delay_ms(300);
  wave01_frequency(as5);
  delay_ms(1000);
  synth.setVibrato(0, 0, 0);

  synth.slidePan(0, 0, -78, 2200);
  wave01_frequency(a5);
  delay_ms(300);
  wave01_frequency(g5);
  delay_ms(200);
  wave01_frequency(f5);
  delay_ms(150);
  wave01_frequency(e5);
  delay_ms(130);
  wave01_frequency(f5);
  delay_ms(120);
  wave01_frequency(e5);
  delay_ms(110);
  wave01_frequency(d5);
  delay_ms(110);
  wave01_frequency(cs5);
  delay_ms(110);
  wave01_frequency(d5);
  delay_ms(110);
  wave01_frequency(c5);
  delay_ms(110);
  wave01_frequency(as4);
  delay_ms(110);
  wave01_frequency(a4);
  delay_ms(110);
  wave01_frequency(g4);
  delay_ms(110);
  wave01_frequency(f4);
  delay_ms(120);
  wave01_frequency(e4);
  delay_ms(130);
  wave01_frequency(d4);
  delay_ms(150);

  counter = 75;
  while (counter > 14) {
    counter--;
    wave01_volume(counter);
    delay_ms(1);
  }

  synth.setBusReverb(0, 0, 200, 125, 255, 96);
  synth.setPan(0, -82);
  synth.setPan(1, 82);
  synth.setPan(2, -45);
  synth.setPan(3, 45);
  synth.setPan(4, -108);
  synth.setPan(5, 108);

  wave09_volume(75);
  wave02_volume(14);
  wave03_volume(14);
  wave04_volume(14);
  wave05_volume(14);
  wave06_volume(14);
  wave09_frequency(g2);
  wave01_frequency(cs5);
  wave02_frequency(e4);
  wave03_frequency(cs4);
  wave04_frequency(g4);
  wave05_frequency(as4);
  wave06_frequency(e5);
  delay_ms(1500);

  stop09();
  stop03();
  stop04();
  stop05();
  stop06();
  counter = 14;
  while (counter < 60) {
    counter++;
    wave01_volume(counter);
    wave02_volume(counter);
    delay_ms(1);
  }
  delay_ms(500);

  // --- Section 9: Two-Voice Baroque Duet Trading Sides in Stereo ---
  counter = 0;
  while (counter != 4) {
    wave01_frequency(cs5);
    wave02_frequency(e4);
    delay_ms(110);
    wave01_frequency(e5);
    wave02_frequency(g4);
    delay_ms(110);
    wave01_frequency(cs5);
    wave02_frequency(e4);
    delay_ms(110);
    wave01_frequency(as4);
    wave02_frequency(cs4);
    delay_ms(110);
    wave01_frequency(cs5);
    wave02_frequency(e4);
    delay_ms(110);
    wave01_frequency(as4);
    wave02_frequency(cs4);
    delay_ms(110);
    counter++;
  }

  synth.slidePan(0, -82, 82, 2600);
  synth.slidePan(1, 82, -82, 2600);
  counter = 0;
  while (counter != 4) {
    wave01_frequency(g4);
    wave02_frequency(as3);
    delay_ms(110);
    wave01_frequency(as4);
    wave02_frequency(cs4);
    delay_ms(110);
    wave01_frequency(g4);
    wave02_frequency(as3);
    delay_ms(110);
    wave01_frequency(e4);
    wave02_frequency(g3);
    delay_ms(110);
    wave01_frequency(g4);
    wave02_frequency(as3);
    delay_ms(110);
    wave01_frequency(e4);
    wave02_frequency(g3);
    delay_ms(110);
    counter++;
  }

  synth.slidePan(0, 82, -82, 2600);
  synth.slidePan(1, -82, 82, 2600);
  counter = 0;
  while (counter != 4) {
    wave01_frequency(cs4);
    wave02_frequency(e3);
    delay_ms(110);
    wave01_frequency(e4);
    wave02_frequency(g3);
    delay_ms(110);
    wave01_frequency(cs4);
    wave02_frequency(e3);
    delay_ms(110);
    wave01_frequency(as3);
    wave02_frequency(cs3);
    delay_ms(110);
    wave01_frequency(cs4);
    wave02_frequency(e3);
    delay_ms(110);
    wave01_frequency(as3);
    wave02_frequency(cs3);
    delay_ms(110);
    counter++;
  }

  synth.slidePanTo(0, -28, 2600);
  synth.slidePanTo(1, 28, 2600);
  counter = 0;
  while (counter != 4) {
    wave01_frequency(cs4);
    wave02_frequency(e3);
    delay_ms(110);
    wave01_frequency(e4);
    wave02_frequency(g3);
    delay_ms(110);
    wave01_frequency(cs4);
    wave02_frequency(e3);
    delay_ms(110);
    wave01_frequency(e4);
    wave02_frequency(g3);
    delay_ms(110);
    wave01_frequency(g4);
    wave02_frequency(as3);
    delay_ms(110);
    wave01_frequency(e4);
    wave02_frequency(g3);
    delay_ms(110);
    counter++;
  }

  counter = 0;
  while (counter != 3) {
    wave01_frequency(g4);
    wave02_frequency(e3);
    delay_ms(110);
    wave01_frequency(as4);
    wave02_frequency(g3);
    delay_ms(110);
    counter++;
  }

  counter = 0;
  while (counter != 3) {
    wave01_frequency(g4);
    wave02_frequency(as3);
    delay_ms(110);
    wave01_frequency(as4);
    wave02_frequency(cs4);
    delay_ms(110);
    counter++;
  }

  wave01_frequency(cs5);
  wave02_frequency(e4);
  delay_ms(110);
  wave01_frequency(as4);
  wave02_frequency(cs4);
  delay_ms(110);

  counter = 0;
  while (counter != 4) {
    wave01_frequency(cs5);
    wave02_frequency(e4);
    delay_ms(110);
    wave01_frequency(e5);
    wave02_frequency(cs4);
    delay_ms(110);
    counter++;
  }

  delay_ms(200);
  counter = 60;
  while (counter > 12) {
    counter--;
    wave01_volume(counter);
    wave02_volume(counter);
    delay_ms(1);
  }

  // --- Section 10: Grand Finale & Final Cathedral Cadence ---
  synth.setPan(0, -110);
  synth.setPan(1, 110);
  synth.setPan(2, -78);
  synth.setPan(3, 78);
  synth.setPan(4, -46);
  synth.setPan(5, 46);
  synth.setPan(6, 0);

  wave09_volume(73);
  wave03_volume(12);
  wave04_volume(12);
  wave05_volume(12);
  wave06_volume(12);
  wave07_volume(12);
  wave09_frequency(g2);
  wave01_frequency(a3);
  wave02_frequency(cs4);
  wave03_frequency(e4);
  wave04_frequency(a4);
  wave05_frequency(cs5);
  wave06_frequency(e5);
  wave07_frequency(a5);
  delay_ms(1000);

  wave09_frequency(f2);
  wave02_frequency(d4);
  wave03_frequency(f4);
  wave05_frequency(d5);
  wave06_frequency(f5);
  delay_ms(1200);

  stop06();
  stop07();
  wave09_volume(95);
  wave01_volume(15);
  wave02_volume(15);
  wave03_volume(15);
  wave04_volume(15);
  wave05_volume(15);
  wave09_frequency(as2);
  wave01_frequency(g4);
  wave03_frequency(as4);
  wave04_frequency(g5);
  delay_ms(1500);

  stop01();
  stop02();
  stop03();
  stop04();
  stop05();

  // Open Bus 2 filter briefly for dramatic pedal reed power
  synth.setBusFilter(2, 0, 5400, 95, FX_FILTER_LPF, 255);
  counter = 95;
  while (counter != 255) {
    counter++;
    wave09_volume(counter);
    delay_ms(1);
  }
  delay_ms(1000);

  wave09_frequency(a2);
  delay_ms(600);
  wave09_frequency(g2);
  delay_ms(400);
  counter = 255;
  while (counter != 75) {
    counter--;
    wave09_volume(counter);
    delay_ms(1);
  }

  synth.setBusFilter(2, 0, 3800, 85, FX_FILTER_LPF, 255);
  wave01_volume(14);
  wave02_volume(14);
  wave03_volume(14);
  wave04_volume(14);
  wave05_volume(14);
  wave06_volume(14);
  wave09_frequency(a2);
  wave01_frequency(cs4);
  wave02_frequency(e4);
  wave03_frequency(a4);
  wave04_frequency(cs5);
  wave05_frequency(e5);
  wave06_frequency(g5);
  delay_ms(1500);

  stop01();
  stop02();
  stop03();
  stop04();
  stop05();
  stop06();
  synth.setBusFilter(2, 0, 5400, 95, FX_FILTER_LPF, 255);
  wave09_frequency(e2);
  counter = 74;
  while (counter != 255) {
    counter++;
    wave09_volume(counter);
    delay_ms(1);
  }
  delay_ms(400);

  wave09_frequency(f2);
  delay_ms(400);
  wave09_frequency(d2);
  delay_ms(300);
  wave09_frequency(e2);
  delay_ms(250);
  wave09_frequency(cs2);
  delay_ms(200);
  wave09_frequency(d2);
  delay_ms(200);
  wave09_frequency(b1);
  delay_ms(200);
  wave09_frequency(cs2);
  delay_ms(200);
  wave09_frequency(a1);
  delay_ms(250);
  wave09_frequency(as1);
  delay_ms(300);
  wave09_frequency(gs1);
  delay_ms(400);
  wave09_frequency(a1);
  delay_ms(400);

  counter = 255;
  while (counter != 103) {
    counter--;
    wave09_volume(counter);
    delay_ms(1);
  }

  synth.setBusFilter(2, 0, 3600, 85, FX_FILTER_LPF, 255);
  synth.setPan(0, -64);
  synth.setPan(1, 64);
  synth.setPan(2, -32);
  synth.setPan(3, 32);

  wave01_volume(18);
  wave02_volume(18);
  wave03_volume(18);
  wave04_volume(18);
  wave09_frequency(g2);
  wave01_frequency(a3);
  wave02_frequency(e4);
  wave03_frequency(cs5);
  wave04_frequency(a4);
  delay_ms(500);

  wave09_volume(95);
  wave01_volume(15);
  wave02_volume(15);
  wave03_volume(15);
  wave04_volume(15);
  wave05_volume(15);
  wave09_frequency(f2);
  wave02_frequency(d4);
  wave03_frequency(f4);
  wave05_frequency(d5);
  delay_ms(1000);

  stop04();
  stop05();
  wave09_volume(123);
  wave01_volume(21);
  wave02_volume(21);
  wave03_volume(21);
  wave09_frequency(d2);
  delay_ms(1500);

  wave09_frequency(a1);
  wave03_frequency(e4);
  delay_ms(1500);
  wave02_frequency(cs4);
  delay_ms(1000);
  wave01_frequency(g3);
  delay_ms(1000);

  // Monumental final D minor chord with expanded Cathedral Reverb
  synth.setBusReverb(0, 0, 220, 120, 255, 118);
  synth.slidePanTo(0, -18, 2500);
  synth.slidePanTo(1, 75, 2500);
  synth.slidePanTo(2, -75, 2500);
  synth.slidePanTo(3, 35, 2500);

  wave09_volume(103);
  wave01_volume(27);
  wave02_volume(15);
  wave03_volume(15);
  wave04_volume(15);
  wave09_frequency(d1);
  wave02_frequency(d3);
  wave03_frequency(a3);
  wave04_frequency(d4);
  delay_ms(2000);

  wave01_frequency(f3);
  delay_ms(1000);
  wave01_frequency(e3);
  delay_ms(1500);
  wave01_frequency(f3);
  delay_ms(5000);

  stop01();
  stop02();
  stop03();
  stop04();
  stop05();
  stop06();
  stop07();
  stop08();
  stop09();
  stop10();
  stop11();
  delay_ms(5000);
}