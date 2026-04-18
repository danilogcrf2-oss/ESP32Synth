#include <ESP32Synth.h>

ESP32Synth synth;

const int CH_BASS_SAW = 0;
const int CH_BASS_SUB = 1; 
const int CH_CHORDS   = 2; 
const int CH_LEAD     = 7;
const int CH_ARP      = 8;
const int CH_COUNTER  = 9; 

#define TAPE_LEN 20000 
int32_t* abyssTape = nullptr;
int writeHead = 0;

// Estados dos filtros
int32_t lpState = 0;          // Low-Pass (Escurece o som)

void IRAM_ATTR theAbyssDSP(int32_t* mixBuffer, int numSamples) {
    if (!abyssTape) return; 

    for (int i = 0; i < numSamples; i++) {
        int32_t dry = mixBuffer[i];

        // 1. Leituras Seguras do Buffer Circular (Sem usar o operador '%')
        // Escolhemos tempos primos menores que TAPE_LEN (20000)
        int tap1 = writeHead - 4327;  if (tap1 < 0) tap1 += TAPE_LEN;
        int tap2 = writeHead - 11003; if (tap2 < 0) tap2 += TAPE_LEN;
        int tap3 = writeHead - 19013; if (tap3 < 0) tap3 += TAPE_LEN;

        // Soma as 3 cabeças e divide por 4 (>> 2) para não clipar
        int32_t wet = (abyssTape[tap1] >> 2) + (abyssTape[tap2] >> 2) + (abyssTape[tap3] >> 2); 

        // 2. Filtro Low-Pass (Deixa as repetições cada vez mais abafadas)
        lpState = ((wet * 50) + (lpState * 206)) >> 8; 

        // 3. Calcula o Feedback para gravar na fita (~78% de feedback)
        int32_t feedback = (dry >> 1) + ((lpState * 200) >> 8);

        // Se a matemática tentar explodir, esmagamos o som no limite do 16-bit.
        if (feedback > 32767) feedback = 32767;
        else if (feedback < -32768) feedback = -32768;

        // Grava na fita e avança
        abyssTape[writeHead] = feedback;
        writeHead++;
        if (writeHead >= TAPE_LEN) writeHead = 0;

        // Mixagem Master
        mixBuffer[i] = (dry) + (lpState << 1);
    }
}

void setup(){
  synth.begin(4, 15, 2, I2S_32BIT); // just put an PCM5102A module side to the esp32 on the protoboard.
  synth.setMasterVolume(200);
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
  synth.slideFreqTo(CH_LEAD, fs5, 2000); delay(2000); // Melodia atinge o F# agudo
  
  chordOff(); synth.noteOff(CH_COUNTER); synth.noteOff(CH_LEAD); delay(100);

  chord(cs2, gs4, cs5, fs5, gs5, cs6);
  synth.setArpeggio(CH_ARP, 60, cs5, fs5, gs5, cs6); // Arpejo fica muito rápido (efeito de "redemoinho")
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
  part1_Intro();
  part2_Improv_Csm();
  part3_Modulation_Fsm();
  part4_Finalization();

  delay(5000); 
}