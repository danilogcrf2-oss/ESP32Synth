#ifndef ESP32_SYNTH_NOTES_H
#define ESP32_SYNTH_NOTES_H
#include <Arduino.h>

// Centi-Hz (Hz * 100)
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

#define c1 3270
#define cs1 3465
#define d1 3671
#define ds1 3889
#define e1 4120
#define f1 4365
#define fs1 4625
#define g1 4900
#define gs1 5191
#define a1 5500
#define as1 5827
#define b1 6174

#define c2 6541
#define cs2 6930
#define d2 7342
#define ds2 7778
#define e2 8241
#define f2 8731
#define fs2 9250
#define g2 9800
#define gs2 10383
#define a2 11000
#define as2 11654
#define b2 12347

#define c3 13081
#define cs3 13859
#define d3 14683
#define ds3 15556
#define e3 16481
#define f3 17461
#define fs3 18500
#define g3 19600
#define gs3 20765
#define a3 22000
#define as3 23308
#define b3 24694

#define c4 26163
#define cs4 27718
#define d4 29366
#define ds4 31113
#define e4 32963
#define f4 34923
#define fs4 36999
#define g4 39200
#define gs4 41530
#define a4 44000
#define as4 46616
#define b4 49388

#define c5 52325
#define cs5 55437
#define d5 58733
#define ds5 62225
#define e5 65925
#define f5 69846
#define fs5 73999
#define g5 78399
#define gs5 83061
#define a5 88000
#define as5 93233
#define b5 98777

#define c6 104650
#define cs6 110873
#define d6 117466
#define ds6 124451
#define e6 131851
#define f6 139691
#define fs6 147998
#define g6 156798
#define gs6 166122
#define a6 176000
#define as6 186466
#define b6 197553

#define c7 209300
#define cs7 221746
#define d7 234932
#define ds7 248902
#define e7 263702
#define f7 279383
#define fs7 295996
#define g7 313596
#define gs7 332244
#define a7 352000
#define as7 372931
#define b7 395107
#define c8 418601
#define cs8 443492
#define d8 469864
#define ds8 497803
#define e8 527404
#define f8 558765
#define fs8 591991
#define g8 627193
#define gs8 664488
#define a8 704000
#define as8 745862
#define b8 790213

// --- OITAVA 9 (Ultrassom para humanos velhos, audível pra jovens e gatos) ---
#define c9 837202
#define cs9 886984
#define d9 939727
#define ds9 995606
#define e9 1054808
#define f9 1117530
#define fs9 1183982
#define g9 1254385
#define gs9 1328975
#define a9 1408000
#define as9 1491724
#define b9 1580427

// --- OITAVA 10 (O Limite de Nyquist @ 48kHz/52kHz) ---
// Cuidado: Nessas frequências, qualquer wavetable que não seja SINE pode distorcer.
#define c10 1674404
#define cs10 1773969
#define d10 1879455
#define ds10 1991213
#define e10 2109616
#define f10 2235061
#define fs10 2367964
#define g10 2508771

inline uint32_t midiToFreq(uint8_t note) {
    return (uint32_t)(44000.0 * pow(2.0, (note - 69) / 12.0));
}
#endif