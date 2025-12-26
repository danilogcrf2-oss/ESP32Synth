ESP32Synth (v1.0) - Motor de Síntese Polifônica Profissional

O ESP32Synth é uma biblioteca de áudio de alta performance desenvolvida para transformar o ESP32 em um sintetizador profissional com 32 vozes independentes. O projeto foi arquitetado para priorizar a velocidade de processamento, garantindo um sinal limpo, sem jitter ou travamentos, mesmo sob carga máxima.


1. Visão Geral e Objetivos

Polifonia de 32 Vozes: Processamento simultâneo de até 32 osciladores independentes.

Saída de Alta Fidelidade: Áudio de 16 bits nativos transmitido via I2S PDM a uma taxa de 48kHz.

Baixo Consumo de CPU: Utilização de Tabelas de Busca (LUT) e matemática de inteiros para preservar os recursos do processador.

Fácil Integração: Sistema de notas simplificado e controle total por meio de funções de alto e baixo nível.



2. Arquitetura do Sistema

Processamento Dual-Core: A renderização de áudio ocorre de forma isolada em uma tarefa dedicada, evitando que interrupções do código principal causem estalos no som.

Gerenciamento de Memória: Todo o conteúdo de áudio e as Wavetables são carregados diretamente na RAM para acesso instantâneo.

Mixagem de 32 bits: As vozes são somadas em um acumulador de 32 bits para evitar distorções (clipping) antes da compressão final para a saída de 16 bits.

Polimorfismo de Bits: Suporte nativo para amostras de 4, 8 e 16 bits, permitindo economizar memória ou buscar estéticas sonoras específicas.



3. Recursos de Síntese

Formas de Onda: Suporte a ondas Senoidal, Triangular, Dente de Serra, Pulso (PWM) e gerador de Ruído.

PWM (Pulse Width Modulation): Controle dinâmico da largura de pulso da onda quadrada (0-255).

ADSR Engine: Envelopes de volume independentes por voz com precisão de 28 bits para transições suaves.


Filtros SVF: Filtros de Estado Variável (Chamberlin) com modos Low-Pass, High-Pass e Band-Pass.


Wavetables Dinâmicas: O motor aceita tabelas de qualquer tamanho, processando os dados conforme a profundidade de bits definida.



4. Sistema de Notas e Frequência
A biblioteca adota uma nomenclatura musical simplificada para facilitar a programação:


Nomenclatura: Notas identificadas por letras minúsculas (ex: c4 para Dó 4) e sustenidos identificados pela letra s (ex: cs4 para Dó Sustenido 4).



Tabela de Busca: Frequências pré-calculadas em Centi-Hz garantem precisão sem o custo computacional de cálculos de ponto flutuante em tempo real.



5. Organização de Arquivos
O projeto é dividido em três módulos principais para máxima organização:


ESP32Synth.h: Definições das estruturas de voz, motor de síntese e macros de hardware.


ESP32Synth.cpp: Implementação do driver I2S, algoritmos de renderização e mixagem.


ESP32SynthNotes.h: Mapeamento completo de frequências musicais e constantes de notas.




6. Controle e Automação

Resolução de 8 bits: Parâmetros como Volume, Filtro e PWM operam em uma escala de 0 a 255, facilitando a integração com potenciômetros e mensagens MIDI CC.


Controle Bruto: Possibilidade de alterar parâmetros de áudio em tempo real sem a necessidade de reiniciar o ciclo do envelope.



7. Calibração de Hardware
Devido a variações nos cristais osciladores de diferentes módulos ESP32, a taxa de amostragem real pode variar levemente, afetando a afinação. Utilize o procedimento abaixo para calibrar seu dispositivo:

7.1 Procedimento de Teste
Toque uma nota de referência, como o Lá 4 (a4 - 440Hz).

Utilize um afinador de precisão para medir a frequência real de saída.

7.2 Cálculo de Correção
Caso a frequência esteja incorreta, aplique a seguinte fórmula para obter o novo valor da constante SYNTH_RATE:

Nova_Taxa = Taxa_Atual * (Freq_Medida / Freq_Esperada)

Exemplo:

Taxa Atual: 52036

Frequência Medida: 406 Hz

Frequência Esperada: 440 Hz

Cálculo: 52036 * (406 / 440) = 48014

7.3 Aplicação
Abra o arquivo ESP32Synth.h e atualize o valor definido para o seu hardware:


#define SYNTH_RATE 48014 // Valor calibrado


Desenvolvido por Danilo.
