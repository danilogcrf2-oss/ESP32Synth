BitDepth#include <ESP32Synth.h>
#include "Sample.h"
ESP32Synth synth;

void setup(){
  synth.begin(26);
  synth.setWave(0,WAVE_SAMPLE);
  synth.registerSample(0, Barbeiro_cegolow_data, Barbeiro_cegolow_len, Barbeiro_cegolow_rate, c4, BITS_4);
  // Agora podemos diminuir a profundidade de bits sem ter que diminuir muito a taxa de amostragem
  synth.setSample(0,0,LOOP_FORWARD, 0, 0);
  synth.noteOn(0,c4,255);
}
void loop(){

}