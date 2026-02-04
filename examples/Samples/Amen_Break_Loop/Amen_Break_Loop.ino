#include "Sample.h" 
#include "ESP32Synth.h"

ESP32Synth synth;

// --- Configuração para setup() ---
const SampleZone zonas_amen_break_44100hz[] = {
    { c0, g10, 0, c4 } 
};

Instrument_Sample inst_amen_break_44100hz = {
    zonas_amen_break_44100hz, // O const SampleZone de cima 
    1, // Quantas zonas
    LOOP_FORWARD, // Modo de loop 
    0, // inicio do loop
    0  // fim do loop ( 0 = ultimo sample)
};

const int BCK_PIN = 19;
const int WS_PIN = 5;
const int DATA_PIN = 18;

void setup() {
    Serial.begin(115200);
    
    synth.begin(BCK_PIN, WS_PIN, DATA_PIN)
    synth.registerSample(0, amen_break_44100hz_data, amen_break_44100hz_len, amen_break_44100hz_rate, c4);
    synth.setInstrument(0, &inst_amen_break_44100hz);

    synth.setEnv(0,0,0,255,0); // envelope simples
    synth.noteOn(0,c4,255);
}

void loop() {
 // nada aqui! o core 1 ja cuida do audio e do loop!
}