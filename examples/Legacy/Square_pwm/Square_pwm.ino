#include "ESP32Synth.h"

ESP32Synth synth;

// Variáveis de controle do PWM
int currentPW = 128; // Começa em 50% (128 de 255)
int direction = 1;   // 1 = Subindo, -1 = Descendo

void setup() {
  // Inicia o Synth nos pinos padrão do S3 Zero que também funcionam no esp32 (wroom32) ajuste se for outro.
  synth.begin(); // pino padrão sempre é o 5. 
  
  // Configura a Voz 0
  synth.setWave(0, WAVE_PULSE);       // Onda Quadrada/Pulso
  synth.setEnv(0, 10, 0, 255, 100);   // Envelope Sustain total
  synth.setVolume(0, 200);            // Volume alto
  
  // Toca uma nota grave (C2) para ouvir bem os harmônicos mudando
  synth.noteOn(0, c2, 200);
  delay(2000);
}

void loop() {
  // 1. Atualiza a largura do pulso
  currentPW += direction;

  // 2. Verifica os limites (0 a 255)
  // Nota: Evitamos o 0 e o 255 total para o som não sumir completamente
  if (currentPW >= 254) {
    direction = -1; // Começa a descer
  } 
  else if (currentPW <= 2) {
    direction = 1;  // Começa a subir
  }

  // 3. Aplica o PWM na Voz 0
  synth.setPulseWidth(0, currentPW);

  // 4. Pequeno delay para controlar a velocidade do efeito
  // 10ms = Sweep rápido
  // 30ms = Sweep lento e detalhado
  delay(15); 
}
