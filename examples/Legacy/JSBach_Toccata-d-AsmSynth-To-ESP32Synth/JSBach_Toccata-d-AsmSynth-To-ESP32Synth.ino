/**
 * @file JSBach_Toccata-d-AsmSynth-To-ESP32Synth.ino
 * @author Danilo Gabriel
 * @brief Plays J.S. Bach's Toccata and Fugue in D minor, BWV 565.
 * 
 * This example is a port of a song originally created for the AsmSynth project.
 * It demonstrates the synthesizer's ability to play complex, multi-voice music
 * by using a hardcoded sequence of notes and timing.
 * 
 * It uses a custom set of note definitions and helper functions to control
 * multiple voices, simulating a tracker-style playback.
 * 
 * It uses the I2S output. Make sure you have an I2S DAC connected.
 * - BCK_PIN:  Your I2S bit clock pin
 * - WS_PIN:   Your I2S word select (LRC) pin
 * - DATA_PIN: Your I2S data out pin
 */
#include "ESP32Synth.h"

// --- Pin Configuration for I2S DAC ---
// --- You MUST change these pins to match your setup ---
const int BCK_PIN = 26;
const int WS_PIN = 25;
const int DATA_PIN = 22;

ESP32Synth synth;

// --- Note Definitions (from original AsmSynth port) ---
// These definitions are local to this sketch to maintain compatibility
// with the original song data. They may differ from the standard
// ESP32SynthNotes.h definitions (e.g., 'h1' is used for 'b1').
#define c0 1635
#define cs0 1732
#define d0 1835
#define ds0 1945
#define e0 2060
#define f0 2183
#define fs0 2312
#define g0 2450
#define gs0 2596
#define a0 2750
#define as0 2914
#define b0 3087

#define c1 (c0 * 2)
#define cs1 (cs0 * 2)
#define d1 (d0 * 2)
#define ds1 (ds0 * 2)
#define e1 (e0 * 2)
#define f1 (f0 * 2)
#define fs1 (fs0 * 2)
#define g1 (g0 * 2)
#define gs1 (gs0 * 2)
#define a1 (a0 * 2)
#define as1 (as0 * 2)
#define h1 (b0 * 2)

#define c2 (c1 * 2)
#define cs2 (cs1 * 2)
#define d2 (d1 * 2)
#define ds2 (ds1 * 2)
#define e2 (e1 * 2)
#define f2 (f1 * 2)
#define fs2 (fs1 * 2)
#define g2 (g1 * 2)
#define gs2 (gs1 * 2)
#define a2 (a1 * 2)
#define as2 (as1 * 2)
#define h2 (h1 * 2)

#define c3 (c2 * 2)
#define cs3 (cs2 * 2)
#define d3 (d2 * 2)
#define ds3 (ds2 * 2)
#define e3 (e2 * 2)
#define f3 (f2 * 2)
#define fs3 (fs2 * 2)
#define g3 (g2 * 2)
#define gs3 (gs2 * 2)
#define a3 (a2 * 2)
#define as3 (as2 * 2)
#define h3 (h2 * 2)

#define c4 (c3 * 2)
#define cs4 (cs3 * 2)
#define d4 (d3 * 2)
#define ds4 (ds3 * 2)
#define e4 (e3 * 2)
#define f4 (f3 * 2)
#define fs4 (fs3 * 2)
#define g4 (g3 * 2)
#define gs4 (gs3 * 2)
#define a4 (a3 * 2)
#define as4 (as3 * 2)
#define h4 (h3 * 2)

#define c5 (c4 * 2)
#define cs5 (cs4 * 2)
#define d5 (d4 * 2)
#define ds5 (ds4 * 2)
#define e5 (e4 * 2)
#define f5 (f4 * 2)
#define fs5 (fs4 * 2)
#define g5 (g4 * 2)
#define gs5 (gs4 * 2)
#define a5 (a4 * 2)
#define as5 (as4 * 2)
#define h5 (h4 * 2)

volatile uint8_t counter = 0;
void delay_ms(int ms) {
  delay(ms);
}

uint8_t boost(int v) {
  int val = v * 2;
  return (val > 255) ? 255 : val;
}

void wave01_volume(int v) {
  synth.setVolume(0, boost(v));
}
void wave02_volume(int v) {
  synth.setVolume(1, boost(v));
}
void wave03_volume(int v) {
  synth.setVolume(2, boost(v));
}
void wave04_volume(int v) {
  synth.setVolume(3, boost(v));
}
void wave05_volume(int v) {
  synth.setVolume(4, boost(v));
}
void wave06_volume(int v) {
  synth.setVolume(5, boost(v));
}
void wave07_volume(int v) {
  synth.setVolume(6, boost(v));
}
void wave08_volume(int v) {
  synth.setVolume(7, boost(v));
}
void wave09_volume(int v) {
  synth.setVolume(8, v);
}
void wave10_volume(int v) {
  synth.setVolume(9, v);
}
void wave11_volume(int v) {
  synth.setVolume(10, v);
}


void wave01_frequency(int f) {
  synth.setFrequency(0, f);
}
void wave02_frequency(int f) {
  synth.setFrequency(1, f);
}
void wave03_frequency(int f) {
  synth.setFrequency(2, f);
}
void wave04_frequency(int f) {
  synth.setFrequency(3, f);
}
void wave05_frequency(int f) {
  synth.setFrequency(4, f);
}
void wave06_frequency(int f) {
  synth.setFrequency(5, f);
}
void wave07_frequency(int f) {
  synth.setFrequency(6, f);
}
void wave08_frequency(int f) {
  synth.setFrequency(7, f);
}
void wave09_frequency(int f) {
  synth.setFrequency(8, f);
}
void wave10_frequency(int f) {
  synth.setFrequency(9, f);
}
void wave11_frequency(int f) {
  synth.setFrequency(10, f);
}


void stop01() {
  wave01_volume(0);
}
void stop02() {
  wave02_volume(0);
}
void stop03() {
  wave03_volume(0);
}
void stop04() {
  wave04_volume(0);
}
void stop05() {
  wave05_volume(0);
}
void stop06() {
  wave06_volume(0);
}
void stop07() {
  wave07_volume(0);
}
void stop08() {
  wave08_volume(0);
}
void stop09() {
  wave09_volume(0);
}
void stop10() {
  wave10_volume(0);
}
void stop11() {
  wave11_volume(0);
}


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
  wave09_volume(127);
  wave10_volume(127);
}
void voice02s() {
  stop09();
  stop10();
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

void setup() {
  Serial.begin(115200);
  delay(1000); 

  Serial.println("ESP32Synth - J.S. Bach's Toccata in D minor");
  Serial.printf("Using ESP32 core %s | Sample Rate: %d Hz\n", synth.getChipModel(), (int)synth.getSampleRate());
  Serial.println("-------------------------------------------------");
  
  // Initialize the synth for I2S output on the specified pins, running on core 1.
  if (!synth.begin(BCK_PIN, WS_PIN, DATA_PIN)) {
    Serial.println("!!! ERROR: Failed to initialize synthesizer.");
    while (1) delay(1000);
  }

  // --- Configure Voices for the Song ---
  // Voices 0-7: Main organ sound (Pulse wave).
  for (int i = 0; i <= 7; i++) {
    synth.setWave(i, WAVE_PULSE);
  }
  // Voices 8-9: Secondary/accompaniment voices (Saw wave).
  synth.setWave(8, WAVE_SAW);
  synth.setWave(9, WAVE_SAW);
  // Voice 10: Bass or pedal voice (Triangle wave).
  synth.setWave(10, WAVE_TRIANGLE);

  // Set a very long, sustained envelope for all voices.
  // This mimics a tracker-style playback where notes are manually stopped.
  for (int i = 0; i <= 10; i++) {
    synth.setEnv(i, 0, 0, 127, 60000);
    // Turn all voices "on" but with 0 volume.
    // The song's logic will then control volume and frequency directly.
    synth.noteOn(i, 44000, 0);
  }
  Serial.println("Synth initialized. Starting playback...");
}

void loop() {
  voice01v();
  voice01(a4);
  delay_ms(100);
  voice01(g4);
  delay_ms(100);
  voice01(a4);
  delay_ms(1500);
  voice01s();
  delay_ms(1500);
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
  voice01v();
  voice01(a2);
  delay_ms(100);
  voice01(g2);
  delay_ms(100);
  voice01(a2);
  delay_ms(1500);
  voice01s();
  delay_ms(1500);
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

  wave09_volume(150);
  counter = 0;
  while (counter != 2) {
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
    voice03(a5);
    delay_ms(1000);
    voice01s();
    delay_ms(2000);
    counter++;
    wave01_volume(75);
  }

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

  wave09_frequency(d1);
  wave09_volume(255);
  delay_ms(1500);
  counter = 255;
  while (counter > 62) {
    wave09_volume(counter);
    counter--;
    delay_ms(1);
  }

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
  wave01_frequency(h3);
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

  counter = 75;
  while (counter > 23) {
    counter--;
    wave01_volume(counter);
    delay_ms(1);
  }

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
  wave01_frequency(h3);
  delay_ms(120);
  wave01_frequency(cs4);
  delay_ms(150);
  wave01_frequency(a3);
  delay_ms(130);
  wave01_frequency(h3);
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
  wave01_frequency(h4);
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

  wave09_frequency(gs1);
  wave01_frequency(h4);
  wave02_frequency(f4);
  wave03_frequency(d4);
  wave04_frequency(h3);
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
  wave01_frequency(h4);
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
  wave09_frequency(h1);
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
  stop09();
  delay_ms(5000);
  ESP.restart();  // Para manter a precisão no proximo loop
}