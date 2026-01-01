/* SUPER SAW TEST - A Parede de Som */
#include "ESP32Synth.h"
ESP32Synth synth;

void setup() {
    synth.begin(); // Data 5
    
    // Configura 7 vozes como Serra (SAW)
    for(int i=0; i<7; i++) {
        synth.setWave(i, WAVE_SAW);
        synth.setEnv(i, 50, 0, 255, 500); // Envelope longo
    }
}

void loop() {
  //loop de super saw
  playSaw(c2);
  delay(2000);
  playSaw(e2);
  delay(2000);
  playSaw(g2);
  delay(2000);
  playSaw(a2);
  delay(2000);

}
void playSaw(uint32_t baseFreq){
      
        // Desliga tudo antes
        for(int i=0; i<7; i++) synth.noteOff(i);

        // Toca o acorde "Unison"
        synth.noteOn(0, baseFreq, 100);        // Central
        synth.noteOn(1, baseFreq + 150, 80);   // +1.5 Hz
        synth.noteOn(2, baseFreq - 150, 80);   // -1.5 Hz
        synth.noteOn(3, baseFreq + 200, 70);   // +2.0 Hz
        synth.noteOn(4, baseFreq - 200, 70);   // -2.0 Hz
        synth.noteOn(5, baseFreq + 300, 60);   // +3.0 Hz
        synth.noteOn(6, baseFreq - 300, 60);   // -3.0 Hz
}
