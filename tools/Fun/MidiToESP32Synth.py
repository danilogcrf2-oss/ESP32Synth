import sys
import os
import mido
import numpy as np
import pygame

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, 
    QPushButton, QLabel, QFileDialog, QComboBox, QSpinBox, 
    QPlainTextEdit, QSplitter, QGroupBox, QListWidget, QListWidgetItem, QGridLayout, QLineEdit, QTabWidget, QTableWidget, QTableWidgetItem, QHeaderView
)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QPalette, QFont

# --- NOMES GM (General MIDI) PADRÃO ---
GM_NAMES = [
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavi",
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)", "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar harmonics",
    "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    "String Ensemble 1", "String Ensemble 2", "SynthStrings 1", "SynthStrings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "SynthBrass 1", "SynthBrass 2",
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
    "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
    "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
    "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)", "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bag pipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot"
]

# Gera Banco de Dados Inteligente (Wave, Attack, Decay, Sustain, Release)
def get_default_gm_config(index):
    # 0: SINE, 1: TRIANGLE, 2: SAW, 3: PULSE, 4: NOISE
    if index < 8: return [3, 5, 800, 0, 400]        # Pianos
    elif index < 16: return [0, 2, 600, 0, 500]     # Chromatic Perc
    elif index < 24: return [3, 10, 100, 255, 100]  # Organs
    elif index < 32: return [2, 10, 500, 50, 200]   # Guitars
    elif index < 40: return [1, 10, 400, 100, 100]  # Basses
    elif index < 48: return [2, 200, 100, 255, 400] # Strings
    elif index < 56: return [2, 100, 100, 255, 300] # Ensembles
    elif index < 64: return [3, 50, 200, 200, 200]  # Brass
    elif index < 80: return [1, 50, 100, 255, 100]  # Pipes/Reeds
    elif index < 88: return [2, 10, 200, 200, 200]  # Synth Leads
    elif index < 96: return [0, 400, 200, 255, 600] # Synth Pads
    elif index < 112: return [3, 10, 300, 0, 200]   # FX/Ethnic
    else: return [4, 5, 300, 0, 300]                # Percussive/SFX

GM_BANK = [get_default_gm_config(i) for i in range(128)]

# Comandos C++ ESP32Synth Player
CMD_NOTE_OFF = 0x80
CMD_NOTE_ON = 0x90
CMD_CC = 0xB0
CMD_PROG = 0xC0
CMD_PITCH = 0xE0
CMD_WAIT = 0xF0  

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.cur_lang = "pt"
        self.setMinimumSize(1200, 800)
        self.setWindowTitle("MidiToESP32Synth v0.1 beta")
        
        self.raw_midi_path = ""
        self.midi_events = []
        self.total_duration_sec = 0.0
        self.max_global_polyphony = 0
        
        # Audio Engine
        self.audio_ready = False
        self.is_playing = False
        try:
            pygame.mixer.pre_init(44100, -16, 2, 1024)
            pygame.mixer.init()
            self.audio_ready = True
        except Exception as e:
            print(f"Erro no Áudio: {e}")

        self.init_ui()
        self.apply_dark_theme()
        self.refresh_table()

    def init_ui(self):
        main_layout = QVBoxLayout()
        container = QWidget()
        container.setLayout(main_layout)
        self.setCentralWidget(container)

        # 1. Arquivo e Controles Globais
        self.grp_file = QGroupBox("1. Arquivo e Controle Global")
        h_file = QHBoxLayout()
        
        self.btn_load = QPushButton("Carregar MIDI...")
        self.btn_load.clicked.connect(self.load_file_dialog)
        h_file.addWidget(self.btn_load)
        
        self.lbl_name = QLabel("Nome (C++):")
        self.txt_name = QLineEdit("my_song")
        self.txt_name.setFixedWidth(150)
        h_file.addWidget(self.lbl_name)
        h_file.addWidget(self.txt_name)
        
        self.lbl_transpose = QLabel("Transpose:")
        self.spin_transpose = QSpinBox()
        self.spin_transpose.setRange(-24, 24)
        self.spin_transpose.setValue(0)
        self.spin_transpose.valueChanged.connect(self.reparse_midi)
        h_file.addWidget(self.lbl_transpose)
        h_file.addWidget(self.spin_transpose)
        
        self.lbl_info = QLabel("Nenhum arquivo carregado.")
        self.lbl_info.setStyleSheet("font-weight: bold; color: #ccc;")
        h_file.addWidget(self.lbl_info, 1)
        
        self.btn_preview = QPushButton("▶ Ouvir Preview")
        self.btn_preview.setStyleSheet("font-weight: bold; color: #00ff7f;")
        self.btn_preview.clicked.connect(self.play_preview)
        h_file.addWidget(self.btn_preview)
        
        self.grp_file.setLayout(h_file)
        main_layout.addWidget(self.grp_file)

        self.tabs = QTabWidget()
        
        # --- ABA 1: GERADOR DE CÓDIGO ---
        tab_code = QWidget()
        l_code = QVBoxLayout(tab_code)
        
        self.btn_convert = QPushButton("CONVERTER / GERAR CÓDIGO C++")
        self.btn_convert.setMinimumHeight(50)
        self.btn_convert.setStyleSheet("font-weight: bold; font-size: 14px; background-color: #007acc;")
        self.btn_convert.clicked.connect(self.generate_code)
        l_code.addWidget(self.btn_convert)
        
        self.txt_code = QPlainTextEdit()
        self.txt_code.setReadOnly(True)
        self.txt_code.setFont(QFont("Consolas", 10))
        l_code.addWidget(self.txt_code)
        self.tabs.addTab(tab_code, "Exportação (C++)")
        
        # --- ABA 2: BANCO DE INSTRUMENTOS (GM) ---
        tab_bank = QWidget()
        l_bank = QVBoxLayout(tab_bank)
        
        info_bank = QLabel("Aqui você gerencia como o ESP32Synth interpretará os instrumentos MIDI (Program Changes). As alterações aqui afetam o Preview e o Código gerado.")
        info_bank.setStyleSheet("color: #aaa; font-style: italic;")
        l_bank.addWidget(info_bank)
        
        self.table_bank = QTableWidget(128, 6)
        self.table_bank.setHorizontalHeaderLabels(["ID", "Instrumento GM", "Onda (Wave)", "Attack", "Decay", "Sustain", "Release"])
        self.table_bank.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self.table_bank.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table_bank.itemChanged.connect(self.on_table_changed)
        l_bank.addWidget(self.table_bank)
        self.tabs.addTab(tab_bank, "Banco de Instrumentos GM")

        main_layout.addWidget(self.tabs, 1)

    def refresh_table(self):
        self.table_bank.blockSignals(True)
        waves = ["SINE", "TRIANGLE", "SAW", "PULSE", "NOISE"]
        for i in range(128):
            # ID
            item_id = QTableWidgetItem(str(i))
            item_id.setFlags(Qt.ItemFlag.ItemIsEnabled)
            self.table_bank.setItem(i, 0, item_id)
            # Nome
            item_name = QTableWidgetItem(GM_NAMES[i])
            item_name.setFlags(Qt.ItemFlag.ItemIsEnabled)
            self.table_bank.setItem(i, 1, item_name)
            
            # Combobox de Ondas
            cb = QComboBox()
            cb.addItems(waves)
            cb.setCurrentIndex(GM_BANK[i][0])
            cb.currentIndexChanged.connect(lambda idx, row=i: self.update_gm_wave(row, idx))
            self.table_bank.setCellWidget(i, 2, cb)
            
            # ADSR
            self.table_bank.setItem(i, 3, QTableWidgetItem(str(GM_BANK[i][1])))
            self.table_bank.setItem(i, 4, QTableWidgetItem(str(GM_BANK[i][2])))
            self.table_bank.setItem(i, 5, QTableWidgetItem(str(GM_BANK[i][3])))
            self.table_bank.setItem(i, 6, QTableWidgetItem(str(GM_BANK[i][4])))
            
        self.table_bank.blockSignals(False)

    def update_gm_wave(self, row, wave_idx):
        GM_BANK[row][0] = wave_idx

    def on_table_changed(self, item):
        row = item.row()
        col = item.column()
        if col >= 3:
            try:
                val = int(item.text())
                if col == 5 and val > 255: val = 255 # Limite do Sustain
                GM_BANK[row][col - 2] = val
            except ValueError:
                pass # Ignora entradas inválidas

    def load_file_dialog(self):
        file_name, _ = QFileDialog.getOpenFileName(self, "Abrir Arquivo MIDI", "", "MIDI Files (*.mid *.midi);;All Files (*)")
        if not file_name: return
        self.raw_midi_path = file_name
        
        base = os.path.splitext(os.path.basename(file_name))[0]
        base = "".join([c if c.isalnum() else "_" for c in base]).lower()
        self.txt_name.setText(base)
        self.reparse_midi()

    def reparse_midi(self):
        if not self.raw_midi_path: return
        try:
            mid = mido.MidiFile(self.raw_midi_path)
        except Exception as e:
            self.lbl_info.setText(f"Erro ao abrir: {e}")
            return

        self.midi_events = []
        self.total_duration_sec = 0.0
        self.max_global_polyphony = 0
        transpose_val = self.spin_transpose.value()
        
        global_active_notes = set()

        for msg in mid:
            delta_ms = int(msg.time * 1000)
            self.total_duration_sec += msg.time
            
            while delta_ms > 65535:
                self.midi_events.append({"delta": 65535, "msg": CMD_WAIT, "p1": 0, "p2": 0})
                delta_ms -= 65535
                
            cmd = 0; p1 = 0; p2 = 0
            ch = getattr(msg, 'channel', 0) & 0x0F
            
            if msg.type == 'note_on' or msg.type == 'note_off':
                is_on = (msg.type == 'note_on' and msg.velocity > 0)
                cmd = CMD_NOTE_ON | ch if is_on else CMD_NOTE_OFF | ch
                
                p1 = msg.note
                if ch != 9: p1 += transpose_val 
                p1 = max(0, min(127, p1))
                p2 = msg.velocity & 0x7F
                
                if is_on: global_active_notes.add((ch, p1))
                else: global_active_notes.discard((ch, p1))
                    
            elif msg.type == 'control_change':
                cmd = CMD_CC | ch
                p1 = msg.control & 0x7F
                p2 = msg.value & 0x7F
            elif msg.type == 'pitchwheel':
                cmd = CMD_PITCH | ch
                val = msg.pitch + 8192
                p1 = val & 0x7F
                p2 = (val >> 7) & 0x7F
            elif msg.type == 'program_change':
                cmd = CMD_PROG | ch
                p1 = msg.program & 0x7F
                
            if cmd != 0:
                self.midi_events.append({"delta": delta_ms, "msg": cmd, "p1": p1, "p2": p2})
            
            if len(global_active_notes) > self.max_global_polyphony:
                self.max_global_polyphony = len(global_active_notes)

        self.lbl_info.setText(f"Eventos: {len(self.midi_events)} | Duração: {self.total_duration_sec:.1f}s | Poli Máx: {self.max_global_polyphony}")

    # --- MOTOR DE ÁUDIO OFFLINE (SIMULADOR DE SÍNTESE REAL) ---
    def play_preview(self):
        if not self.audio_ready or len(self.midi_events) == 0: return
        
        if self.is_playing:
            pygame.mixer.stop()
            self.is_playing = False
            self.btn_preview.setText("▶ Ouvir Preview")
            return

        self.btn_preview.setText("Processando Síntese (Aguarde)...")
        self.btn_preview.setStyleSheet("font-weight: bold; color: #FFA500;")
        QApplication.processEvents()

        try:
            sr = 44100
            total_samples = int((self.total_duration_sec + 3.0) * sr)
            master_buffer = np.zeros(total_samples, dtype=np.float32)
            
            # Arrays Contínuos de Modulação por Canal
            ch_pitch_bend = np.zeros((16, total_samples), dtype=np.float32)
            ch_volume = np.ones((16, total_samples), dtype=np.float32) # CC7
            ch_expr = np.ones((16, total_samples), dtype=np.float32)   # CC11
            ch_mod = np.zeros((16, total_samples), dtype=np.float32)   # CC1 (Vibrato)
            ch_programs = np.zeros((16, total_samples), dtype=np.int32)
            
            # Estado do Parse
            curr_pb = [0.0]*16
            curr_vol = [1.0]*16
            curr_expr = [1.0]*16
            curr_mod = [0.0]*16
            curr_prog = [0]*16
            curr_prog[9] = -1 # Canal 10 ignorado em Program Change
            
            # Rastreamento de Notas e Sustain
            active_notes = {}  # key: (ch, note) -> val: (start_samp, vel, program)
            rendered_notes = [] # (ch, note, start, end, vel, prog)
            sustain_pedal = [False]*16
            sustained_queue = {i: [] for i in range(16)} # Notas seguradas pelo pedal
            
            current_samp = 0
            
            # --- PASSO 1: Analisar Linha do Tempo ---
            for ev in self.midi_events:
                # Preenche automações até o tempo atual
                delta_samps = int((ev["delta"] / 1000.0) * sr)
                
                # Se o tempo avançou, aplica os valores contínuos guardados
                if delta_samps > 0:
                    nxt = current_samp + delta_samps
                    for i in range(16):
                        ch_pitch_bend[i, current_samp:nxt] = curr_pb[i]
                        ch_volume[i, current_samp:nxt] = curr_vol[i]
                        ch_expr[i, current_samp:nxt] = curr_expr[i]
                        ch_mod[i, current_samp:nxt] = curr_mod[i]
                        ch_programs[i, current_samp:nxt] = curr_prog[i]
                
                current_samp += delta_samps
                
                cmd = ev["msg"] & 0xF0
                ch = ev["msg"] & 0x0F
                
                if cmd == 0x90: # Note On
                    active_notes[(ch, ev["p1"])] = (current_samp, ev["p2"], curr_prog[ch])
                    
                elif cmd == 0x80: # Note Off
                    key = (ch, ev["p1"])
                    if key in active_notes:
                        start_s, vel, prog = active_notes.pop(key)
                        if sustain_pedal[ch]:
                            sustained_queue[ch].append((key, start_s, vel, prog))
                        else:
                            rendered_notes.append((ch, ev["p1"], start_s, current_samp, vel, prog))
                            
                elif cmd == 0xE0: # Pitch Bend
                    pb_val = (ev["p1"] | (ev["p2"] << 7)) - 8192
                    curr_pb[ch] = (pb_val / 8192.0) * 2.0 # +/- 2 semitons
                    
                elif cmd == 0xB0: # CC
                    cc = ev["p1"]
                    val = ev["p2"]
                    if cc == 7: curr_vol[ch] = val / 127.0
                    elif cc == 11: curr_expr[ch] = val / 127.0
                    elif cc == 1: curr_mod[ch] = val / 127.0
                    elif cc == 64: # Sustain Pedal
                        sustain_pedal[ch] = (val >= 64)
                        if not sustain_pedal[ch]:
                            # Soltou o pedal: Encerra todas as notas retidas!
                            for s_key, s_start, s_vel, s_prog in sustained_queue[ch]:
                                rendered_notes.append((s_key[0], s_key[1], s_start, current_samp, s_vel, s_prog))
                            sustained_queue[ch].clear()
                            
                elif cmd == 0xC0: # Program Change
                    if ch != 9: curr_prog[ch] = ev["p1"]

            # Processa notas que ficaram ativas (sem note off no final do arquivo)
            for (ch, note), (start_s, vel, prog) in active_notes.items():
                rendered_notes.append((ch, note, start_s, current_samp, vel, prog))

            # --- PASSO 2: Síntese Acumulada (Matemática Real) ---
            t_master = np.arange(total_samples) / sr
            
            for ch, note, start, end, vel, prog in rendered_notes:
                start = int(start)
                end = int(end)
                if end <= start: continue
                
                # Resgata configuração do GM Bank para o Instrumento no momento do NoteOn
                if ch == 9: cfg = get_default_gm_config(0) # Percussão fixa no Python
                else: cfg = GM_BANK[prog]
                
                a_samp = int((cfg[1] / 1000.0) * sr)
                d_samp = int((cfg[2] / 1000.0) * sr)
                r_samp = int((cfg[4] / 1000.0) * sr)
                s_lev = float(cfg[3] / 255.0)
                
                note_total_len = int((end - start) + r_samp)
                if start + note_total_len >= total_samples:
                    note_total_len = int(total_samples - start - 1)
                if note_total_len <= 0: continue
                
                # Fatias de Modulação
                sl_slice = slice(start, start + note_total_len)
                pb_slice = ch_pitch_bend[ch, sl_slice]
                vol_slice = ch_volume[ch, sl_slice]
                expr_slice = ch_expr[ch, sl_slice]
                mod_slice = ch_mod[ch, sl_slice]
                
                # ADSR Envelope
                env = np.ones(note_total_len, dtype=np.float32) * s_lev
                if a_samp > 0:
                    a_lim = int(min(a_samp, note_total_len))
                    env[:a_lim] = np.linspace(0.0, 1.0, a_lim)
                dec_end = int(min(a_samp + d_samp, note_total_len))
                if d_samp > 0 and dec_end > a_samp:
                    env[a_samp:dec_end] = np.linspace(1.0, s_lev, dec_end - a_samp)
                note_off_idx = int(end - start)
                if note_off_idx < note_total_len:
                    rel_len = int(note_total_len - note_off_idx)
                    current_sus = float(env[note_off_idx - 1]) if note_off_idx > 0 else 0.0
                    env[note_off_idx:] = np.linspace(current_sus, 0.0, rel_len)
                
                # Frequência base + Pitch Bend + Vibrato
                base_freq = 440.0 * (2.0 ** ((note - 69.0) / 12.0))
                lfo_vib = np.sin(2 * np.pi * 5.0 * t_master[sl_slice]) * mod_slice * 0.5 
                total_semis = pb_slice + lfo_vib
                
                freq_arr = base_freq * (2.0 ** (total_semis / 12.0))
                
                # Acúmulo de Fase (Mágica para não ter estalos em slides/bends)
                phase = np.cumsum(freq_arr) / sr
                
                # Gerações das Formas de Onda
                w_type = cfg[0]
                if w_type == 0: wave = np.sin(2 * np.pi * phase)
                elif w_type == 1: wave = 2 * np.abs(2 * (phase % 1) - 1) - 1
                elif w_type == 2: wave = 2 * (phase % 1) - 1
                elif w_type == 3: wave = np.where((phase % 1) < 0.5, 1.0, -1.0)
                else: wave = np.random.uniform(-1.0, 1.0, note_total_len)
                
                # Multiplicadores de Volume (Env * Velocity * CC7 * CC11)
                audio_chunk = wave * env * (vel / 127.0) * vol_slice * expr_slice * 0.1 
                master_buffer[sl_slice] += audio_chunk

            # Normalização Segura
            mx = np.max(np.abs(master_buffer))
            if mx > 1.0: master_buffer /= mx
            
            # Exporta para Pygame
            master_buffer = (master_buffer * 32767).astype(np.int16)
            stereo_buffer = np.repeat(master_buffer[:, None], 2, axis=1) 
            stereo_buffer = np.ascontiguousarray(stereo_buffer)

            sound = pygame.sndarray.make_sound(stereo_buffer)
            sound.play()
            
            self.is_playing = True
            self.btn_preview.setText("❚❚ Stop Áudio")
            self.btn_preview.setStyleSheet("font-weight: bold; color: #ff3333;")
            
        except Exception as e:
            self.btn_preview.setText("Erro!")
            print(f"Erro no renderizador: {e}")

    def generate_code(self):
        if len(self.midi_events) == 0: return
        
        name_raw = self.txt_name.text().strip()
        array_name = "".join([c if c.isalnum() else "_" for c in name_raw])
        if not array_name: array_name = "my_song"
        array_name_upper = array_name.upper()
        
        code = f"/**\n * Gerado por MidiToESP32Synth\n"
        code += f" * Banco GM (128 Instrumentos), Controle Absoluto de Vozes O(1), Sustain e Pitchbend.\n"
        code += f" * Polifonia Máxima Necessária (Aumente o MAX_VOICES se necessário): {self.max_global_polyphony} vozes.\n */\n\n"
        code += f"#ifndef {array_name_upper}_H\n"
        code += f"#define {array_name_upper}_H\n\n"
        code += "#include <Arduino.h>\n"
        code += "#include \"ESP32Synth.h\"\n\n"
        
        code += "#pragma pack(push, 1)\n"
        code += "struct SynthEvent {\n"
        code += "    uint16_t deltaMs;\n"
        code += "    uint8_t  msg;\n"
        code += "    uint8_t  param1;\n"
        code += "    uint8_t  param2;\n"
        code += "};\n"
        code += "#pragma pack(pop)\n\n"
        
        code += "struct MidiChannelConfig {\n"
        code += "    WaveType wave;\n"
        code += "    uint16_t a; uint16_t d; uint8_t s; uint16_t r;\n"
        code += "};\n\n"
        
        # Exporta Banco GM
        code += "// --- BANCO DE INSTRUMENTOS GERAIS (GM) PROGMEM ---\n"
        code += f"const MidiChannelConfig {array_name}_gm_bank[128] PROGMEM = {{\n"
        wave_strings = ["WAVE_SINE", "WAVE_TRIANGLE", "WAVE_SAW", "WAVE_PULSE", "WAVE_NOISE"]
        for i in range(128):
            w_str = wave_strings[GM_BANK[i][0]]
            code += f"    {{{w_str}, {GM_BANK[i][1]}, {GM_BANK[i][2]}, {GM_BANK[i][3]}, {GM_BANK[i][4]}}}, // {i}: {GM_NAMES[i]}\n"
        code += "};\n\n"
        
        # Exporta Eventos
        code += f"const SynthEvent {array_name}_events[] PROGMEM = {{\n"
        for e in self.midi_events:
            code += f"    {{{e['delta']}, 0x{e['msg']:02X}, {e['p1']}, {e['p2']}}},\n"
        code += "};\n\n"
        code += f"const uint32_t {array_name}_size = {len(self.midi_events)};\n\n"

        code += """
// ==========================================================
// O MOTOR DE REPRODUÇÃO (ESP32SynthMidiPlayer)
// ==========================================================
class ESP32SynthMidiPlayer {
private:
    ESP32Synth* synth;
    const SynthEvent* sequence;
    const MidiChannelConfig* gmBank;
    uint32_t seqSize;
    uint32_t currentIndex;
    uint32_t lastTick;
    uint16_t waitMs;
    bool isPlaying;
    
    // Controles de Estado dos Canais
    uint8_t ch_program[16];
    uint8_t ch_volume[16];
    bool ch_sustain[16];
    
    // Gerenciador de Vozes Complexas (Notas Sustentadas)
    struct VoiceState {
        int8_t channel;
        uint8_t note;
        bool sustained;
    } voices[MAX_VOICES];

    // Helpers Matemáticos
    uint32_t midiToFreqBend(uint8_t note, float bendSemis) {
        return (uint32_t)(44000.0 * pow(2.0, (note + bendSemis - 69.0) / 12.0));
    }

    int8_t allocateVoice() {
        for (uint8_t i = 0; i < MAX_VOICES; i++) {
            if (!synth->isVoiceActive(i)) return i;
        }
        return -1; 
    }

public:
    ESP32SynthMidiPlayer() : isPlaying(false) {
        for(int i=0; i<MAX_VOICES; i++) {
            voices[i].channel = -1;
            voices[i].sustained = false;
        }
    }

    void begin(ESP32Synth* s, const SynthEvent* seq, uint32_t size, const MidiChannelConfig* bank) {
        synth = s;
        sequence = seq;
        seqSize = size;
        gmBank = bank;
        currentIndex = 0;
        waitMs = 0;
        isPlaying = false;
        
        for(int i=0; i<16; i++) {
            ch_program[i] = 0; 
            ch_volume[i] = 100;
            ch_sustain[i] = false;
        }
    }

    void play() {
        isPlaying = true;
        lastTick = millis();
    }

    void stop() {
        isPlaying = false;
        for(int v=0; v<MAX_VOICES; v++) synth->noteOff(v);
        for(int i=0; i<MAX_VOICES; i++) voices[i].channel = -1;
    }

    void tick() {
        if (!isPlaying || currentIndex >= seqSize) return;

        uint32_t now = millis();
        if ((now - lastTick) >= waitMs) {
            lastTick = now;
            
            while (currentIndex < seqSize) {
                const SynthEvent& ev = sequence[currentIndex];
                waitMs = ev.deltaMs;
                
                uint8_t cmd = ev.msg & 0xF0;
                uint8_t ch = ev.msg & 0x0F;
                
                if (cmd == 0x90) { // NOTE ON
                    int8_t v = allocateVoice();
                    if (v != -1) {
                        voices[v].channel = ch;
                        voices[v].note = ev.param1;
                        voices[v].sustained = false;
                        
                        // Aplica GM Bank
                        MidiChannelConfig cfg = (ch == 9) ? gmBank[0] : gmBank[ch_program[ch]];
                        if(ch == 9) cfg.wave = WAVE_NOISE; // Override bateria
                        
                        synth->setWave(v, cfg.wave);
                        synth->setEnv(v, cfg.a, cfg.d, cfg.s, cfg.r); 
                        
                        uint8_t finalVol = (ev.param2 * ch_volume[ch]) / 127;
                        synth->noteOn(v, midiToFreqBend(ev.param1, 0), finalVol * 2);
                    }
                } 
                else if (cmd == 0x80) { // NOTE OFF
                    for (int v = 0; v < MAX_VOICES; v++) {
                        if (voices[v].channel == ch && voices[v].note == ev.param1 && !voices[v].sustained) {
                            if (ch_sustain[ch]) {
                                voices[v].sustained = true; // Pedal segurando
                            } else {
                                synth->noteOff(v);
                                voices[v].channel = -1;
                            }
                            break; // Se matou a voz, sai do loop
                        }
                    }
                }
                else if (cmd == 0xC0) { // PROGRAM CHANGE
                    if (ch != 9) ch_program[ch] = ev.param1;
                }
                else if (cmd == 0xE0) { // PITCH BEND
                    int16_t pb = (ev.param1 | (ev.param2 << 7)) - 8192; 
                    float bendSemis = (pb / 8192.0f) * 2.0f;
                    for(int v=0; v<MAX_VOICES; v++) {
                        if(voices[v].channel == ch) synth->setFrequency(v, midiToFreqBend(voices[v].note, bendSemis));
                    }
                }
               else if (cmd == 0xB0) { // CONTROL CHANGE
                    if (ev.param1 == 1) { // MOD WHEEL
                        for(int v=0; v<MAX_VOICES; v++) {
                            if(voices[v].channel == ch) synth->setVibrato(v, 500, ev.param2 * 10); 
                        }
                    }
                    else if (ev.param1 == 7) { // VOLUME MAIN
                        ch_volume[ch] = ev.param2;
                    }
                    else if (ev.param1 == 64) { // SUSTAIN PEDAL
                        ch_sustain[ch] = (ev.param2 >= 64);
                        if (!ch_sustain[ch]) { // Soltou o pedal
                            for (int v = 0; v < MAX_VOICES; v++) {
                                if (voices[v].channel == ch && voices[v].sustained) {
                                    synth->noteOff(v);
                                    voices[v].sustained = false;
                                    voices[v].channel = -1;
                                }
                            }
                        }
                    }
                }
                
                currentIndex++;
                if (currentIndex < seqSize && sequence[currentIndex].deltaMs > 0) {
                    waitMs = sequence[currentIndex].deltaMs;
                    break;
                }
            }
        }
    }
};
#endif
"""
        self.txt_code.setPlainText(code)

    def apply_dark_theme(self):
        palette = QPalette()
        palette.setColor(QPalette.ColorRole.Window, QColor(53, 53, 53))
        palette.setColor(QPalette.ColorRole.WindowText, Qt.GlobalColor.white)
        palette.setColor(QPalette.ColorRole.Base, QColor(25, 25, 25))
        palette.setColor(QPalette.ColorRole.AlternateBase, QColor(53, 53, 53))
        palette.setColor(QPalette.ColorRole.ToolTipBase, Qt.GlobalColor.white)
        palette.setColor(QPalette.ColorRole.ToolTipText, Qt.GlobalColor.white)
        palette.setColor(QPalette.ColorRole.Text, Qt.GlobalColor.white)
        palette.setColor(QPalette.ColorRole.Button, QColor(53, 53, 53))
        palette.setColor(QPalette.ColorRole.ButtonText, Qt.GlobalColor.white)
        palette.setColor(QPalette.ColorRole.BrightText, Qt.GlobalColor.red)
        palette.setColor(QPalette.ColorRole.Link, QColor(42, 130, 218))
        palette.setColor(QPalette.ColorRole.Highlight, QColor(42, 130, 218))
        QApplication.setPalette(palette)
        QApplication.setStyle("Fusion")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())