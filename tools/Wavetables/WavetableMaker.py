import sys
import numpy as np
import pygame
import math

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, 
    QPushButton, QLabel, QComboBox, QSpinBox, QPlainTextEdit, 
    QSplitter, QGroupBox, QStyle, QLineEdit, QDialog, QSlider, QScrollArea, 
    QFrame, QRadioButton, QButtonGroup, QTabWidget, QGridLayout, QSizePolicy, QDoubleSpinBox
)
from PyQt6.QtCore import Qt, pyqtSignal, QTimer, QPointF, QRectF, QRect
from PyQt6.QtGui import QPainter, QColor, QPen, QPalette, QFont, QBrush, QPainterPath

# --- DICIONÁRIO DE TRADUÇÃO ---
TRANSLATIONS = {
    "pt": {
        "window_title": "Criador de Wavetables para ESP32Synth (FM Edition)",
        "config_box": "Configurações da Wavetable",
        "name_lbl": "Nome:",
        "size_lbl": "Tamanho:",
        "res_lbl": "Resolução:",
        "bits_8": "8-bit (Padrão)",
        "bits_16": "16-bit (Hi-Fi)",
        "bits_4": "4-bit (Glitch/Lo-Fi)",
        "tab_formula": "Fórmula",
        "tab_additive": "Aditiva",
        "tab_fm": "Síntese FM (4-Op)", # Nova Aba
        "math_func": "Função Matemática (numpy):",
        "tip_formula": "Dica: Use 'x' para ângulo (0 a 2pi).",
        "reset_add": "Resetar Aditiva",
        "piano_header": "Teste de Áudio (Piano):",
        "octave_lbl": "Oitava Base:",
        "formula_err": "// Erro na fórmula: ",
        "code_note_4bit": "// Nota: Dados quantizados em 4-bit armazenados em container de 8-bit\n",
        "code_setup": "// Use no setup():\n",
        "btn_lang": "🇺🇸 English",
        "fm_algo": "Algoritmo:",
        "fm_feedback": "Feedback (Op4):",
        "op_ratio": "Ratio",
        "op_level": "Level",
        "op_detune": "Detune",
    },
    "en": {
        "window_title": "Wavetable Maker for ESP32Synth (FM Edition)",
        "config_box": "Wavetable Settings",
        "name_lbl": "Name:",
        "size_lbl": "Size:",
        "res_lbl": "Resolution:",
        "bits_8": "8-bit (Standard)",
        "bits_16": "16-bit (Hi-Fi)",
        "bits_4": "4-bit (Glitch/Lo-Fi)",
        "tab_formula": "Formula",
        "tab_additive": "Additive",
        "tab_fm": "FM Synthesis (4-Op)",
        "math_func": "Math Function (numpy):",
        "tip_formula": "Tip: Use 'x' for angle (0 to 2pi).",
        "reset_add": "Reset Additive",
        "piano_header": "Audio Test (Piano):",
        "octave_lbl": "Base Octave:",
        "formula_err": "// Formula Error: ",
        "code_note_4bit": "// Note: 4-bit quantized data stored in 8-bit container\n",
        "code_setup": "// Use in setup():\n",
        "btn_lang": "🇧🇷 Português",
        "fm_algo": "Algorithm:",
        "fm_feedback": "Feedback (Op4):",
        "op_ratio": "Ratio",
        "op_level": "Level",
        "op_detune": "Detune",
    }
}

# --- ENGINE DE ÁUDIO ---
class AudioEngine:
    def __init__(self, sample_rate=44100):
        self.sample_rate = sample_rate
        self.ready = False
        try:
            pygame.mixer.pre_init(sample_rate, -16, 2, 512)
            pygame.mixer.init()
            pygame.mixer.set_num_channels(8)
            self.ready = True
        except Exception as e:
            print(f"Audio Engine Error: {e}")

    def play_tone(self, frequency, waveform_float):
        if not self.ready or len(waveform_float) == 0: return
        pygame.mixer.stop()

        try:
            duration_sec = 2.0
            total_samples = int(self.sample_rate * duration_sec)
            wt_len = len(waveform_float)
            
            t = np.arange(total_samples)
            indices = (t * (wt_len * frequency / self.sample_rate)) % wt_len
            indices = indices.astype(int)
            
            buffer = waveform_float[indices]
            buffer = (buffer * 30000).astype(np.int16)
            buffer = np.repeat(buffer[:, np.newaxis], 2, axis=1)
            buffer = np.ascontiguousarray(buffer)
            
            fade_len = 2000
            if total_samples > fade_len:
                fade = np.linspace(1, 0, fade_len)
                buffer[-fade_len:, 0] = (buffer[-fade_len:, 0] * fade).astype(np.int16)
                buffer[-fade_len:, 1] = (buffer[-fade_len:, 1] * fade).astype(np.int16)

            sound = pygame.sndarray.make_sound(buffer)
            sound.play(loops=-1, fade_ms=10)
        except Exception as e:
            print(f"Playback Error: {e}")

    def stop(self):
        if self.ready: pygame.mixer.fadeout(100)

# --- VISUALIZADOR DE ONDA ---
class WaveformDisplay(QWidget):
    dataEdited = pyqtSignal(np.ndarray)

    def __init__(self):
        super().__init__()
        self.setMinimumHeight(250) # Altura mínima
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding) # Auto-Resize
        
        self.raw_data = np.zeros(256) 
        self.display_data = np.zeros(256) 
        self.is_drawing = False
        self.draw_mode = "manual" 
        self.last_draw_pos = None 
        
        self.color_line = QColor("#00ff7f")
        self.color_fill = QColor(0, 255, 127, 40)
        self.setBackgroundRole(QPalette.ColorRole.Base)
        self.setAutoFillBackground(True)

    def setData(self, data, processed_data):
        self.raw_data = data
        self.display_data = processed_data
        self.update()

    def mousePressEvent(self, event):
        if self.draw_mode == "manual" and event.button() == Qt.MouseButton.LeftButton:
            self.is_drawing = True
            self.last_draw_pos = event.position()
            self.apply_draw_at(event.position())
            self.update()

    def mouseMoveEvent(self, event):
        if self.is_drawing and self.draw_mode == "manual":
            current_pos = event.position()
            self.interpolate_draw(self.last_draw_pos, current_pos)
            self.last_draw_pos = current_pos
            self.update()

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.is_drawing = False
            self.last_draw_pos = None
            self.dataEdited.emit(self.raw_data)

    def interpolate_draw(self, p1, p2):
        if p1 is None: p1 = p2
        x1, y1 = p1.x(), p1.y()
        x2, y2 = p2.x(), p2.y()
        dist = max(abs(x2 - x1), abs(y2 - y1))
        steps = int(dist) + 1
        for i in range(steps):
            t = i / float(steps) if steps > 0 else 0
            x = x1 + (x2 - x1) * t
            y = y1 + (y2 - y1) * t
            self.apply_draw_at(QPointF(x, y))

    def apply_draw_at(self, pos):
        w, h = self.width(), self.height()
        if w == 0: return
        idx = int((pos.x() / w) * len(self.raw_data))
        val = 1.0 - (pos.y() / (h / 2.0))
        idx = max(0, min(idx, len(self.raw_data) - 1))
        val = max(-1.0, min(val, 1.0))
        self.raw_data[idx] = val
        self.display_data[idx] = val 

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#252525"))
        
        painter.setPen(QPen(QColor("#444444"), 1, Qt.PenStyle.DotLine))
        mid_y = self.height() / 2
        painter.drawLine(0, int(mid_y), self.width(), int(mid_y))
        
        if len(self.display_data) < 2: return

        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        path = QPainterPath()
        w = self.width()
        scale_y = (self.height() / 2) * 0.90 
        step_x = w / len(self.display_data)

        start_y = mid_y - (self.display_data[0] * scale_y)
        path.moveTo(0, start_y)
        
        for i in range(len(self.display_data)):
            x = i * step_x
            y = mid_y - (self.display_data[i] * scale_y)
            if i == 0: path.moveTo(x, y)
            else: path.lineTo(x, y)
            path.lineTo(x + step_x, y)

        path_fill = QPainterPath(path)
        path_fill.lineTo(w, mid_y)
        path_fill.lineTo(0, mid_y)
        painter.setBrush(QBrush(self.color_fill))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawPath(path_fill)

        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(QPen(self.color_line, 2))
        painter.drawPath(path)
        
        painter.setPen(QColor("#888"))
        painter.drawText(10, 20, f"Samples: {len(self.display_data)}")

# --- WIDGET AUXILIAR PARA OPERADOR FM ---
class FMOperatorWidget(QGroupBox):
    valueChanged = pyqtSignal()
    
    def __init__(self, index, parent=None):
        super().__init__(f"Operator {index}", parent)
        self.index = index
        self.layout = QGridLayout(self)
        self.layout.setContentsMargins(5, 5, 5, 5)
        self.layout.setSpacing(5)

        # Ratio (Frequência)
        self.lbl_ratio = QLabel("Ratio")
        self.spin_ratio = QDoubleSpinBox()
        self.spin_ratio.setRange(0.0, 32.0)
        self.spin_ratio.setSingleStep(0.5)
        self.spin_ratio.setValue(1.0)
        self.spin_ratio.valueChanged.connect(self.valueChanged.emit)

        # Level (Volume)
        self.lbl_level = QLabel("Level")
        self.slider_level = QSlider(Qt.Orientation.Horizontal)
        self.slider_level.setRange(0, 100)
        self.slider_level.setValue(100 if index == 1 else 0) # Op 1 ativo por padrão
        self.slider_level.valueChanged.connect(self.valueChanged.emit)

        # Detune
        self.lbl_detune = QLabel("Detune")
        self.spin_detune = QDoubleSpinBox()
        self.spin_detune.setRange(-5.0, 5.0)
        self.spin_detune.setSingleStep(0.1)
        self.spin_detune.setValue(0.0)
        self.spin_detune.valueChanged.connect(self.valueChanged.emit)

        # Layout
        self.layout.addWidget(self.lbl_ratio, 0, 0)
        self.layout.addWidget(self.spin_ratio, 0, 1)
        self.layout.addWidget(self.lbl_detune, 0, 2)
        self.layout.addWidget(self.spin_detune, 0, 3)
        
        self.layout.addWidget(self.lbl_level, 1, 0)
        self.layout.addWidget(self.slider_level, 1, 1, 1, 3)

    def get_params(self):
        return {
            "ratio": self.spin_ratio.value(),
            "level": self.slider_level.value() / 100.0,
            "detune": self.spin_detune.value()
        }

# --- WIDGET DE PIANO ---
class PianoWidget(QWidget):
    noteOn = pyqtSignal(int)
    noteOff = pyqtSignal()
    def __init__(self):
        super().__init__()
        self.setFixedHeight(100)
        self.pressed_key = -1
        self.start_note = 48 
        
    def set_octave(self, octave):
        self.start_note = octave * 12
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        num_whites = 14
        key_w = w / num_whites
        whites = [0, 2, 4, 5, 7, 9, 11]
        
        # Teclas Brancas
        for i in range(num_whites):
            x = i * key_w
            note_idx = (i // 7) * 12 + whites[i % 7]
            is_pressed = (self.pressed_key == note_idx)
            r = QRectF(x, 0, key_w, h)
            p.setBrush(QColor(200, 200, 200) if is_pressed else QColor(240, 240, 240))
            p.setPen(QColor(50, 50, 50))
            p.drawRoundedRect(r, 2, 2)
            if i % 7 == 0:
                octave_num = (self.start_note // 12) + (i // 7)
                p.drawText(r.adjusted(0,0,0,-5), Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignHCenter, f"C{octave_num}")

        # Teclas Pretas
        b_w, b_h = key_w * 0.6, h * 0.6
        for i in range(num_whites):
            if (i % 7) in [0, 1, 3, 4, 5]: 
                x = (i + 1) * key_w - (b_w / 2)
                octave, idx_in_oct = i // 7, i % 7
                if idx_in_oct == 0: n = 1 
                elif idx_in_oct == 1: n = 3
                elif idx_in_oct == 3: n = 6
                elif idx_in_oct == 4: n = 8
                elif idx_in_oct == 5: n = 10
                note_idx = octave * 12 + n
                is_pressed = (self.pressed_key == note_idx)
                p.setBrush(QColor(50, 50, 50) if is_pressed else QColor(10, 10, 10))
                p.drawRoundedRect(QRectF(x, 0, b_w, b_h), 2, 2)

    def get_note_at_pos(self, pos):
        w, h = self.width(), self.height()
        num_whites = 14
        key_w = w / num_whites
        b_h, b_w = h * 0.6, key_w * 0.6
        whites = [0, 2, 4, 5, 7, 9, 11]
        white_idx = int(pos.x() / key_w)
        rel_x = pos.x() - (white_idx * key_w)
        
        if pos.y() < b_h:
            if rel_x < b_w/2 and white_idx > 0 and (white_idx%7) in [1,2,4,5,6]:
                prev_white = (white_idx-1)%7
                n = {0:1, 1:3, 3:6, 4:8, 5:10}.get(prev_white, 0)
                return ((white_idx-1)//7)*12 + n
            if rel_x > (key_w - b_w/2) and (white_idx%7) in [0,1,3,4,5]:
                 curr_white = white_idx%7
                 n = {0:1, 1:3, 3:6, 4:8, 5:10}.get(curr_white, 0)
                 return (white_idx//7)*12 + n
        if white_idx >= num_whites: return -1
        return (white_idx//7)*12 + whites[white_idx%7]

    def mousePressEvent(self, event):
        n = self.get_note_at_pos(event.position())
        if n != -1:
            self.pressed_key = n
            self.noteOn.emit(self.start_note + n)
            self.update()

    def mouseReleaseEvent(self, event):
        self.pressed_key = -1
        self.noteOff.emit()
        self.update()

# --- JANELA PRINCIPAL ---
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.cur_lang = "pt"
        self.setWindowTitle("Wavetable Maker")
        self.setMinimumSize(1200, 800)
        
        self.audio = AudioEngine()
        self.raw_data = np.zeros(256) 
        
        self.current_bit_depth = 8 
        self.table_size = 256
        
        self.init_ui()
        self.apply_theme()
        self.update_texts() 
        
        self.generate_formula("sin(x)")

    def init_ui(self):
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        layout = QVBoxLayout(main_widget)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(10)

        # 0. BOTÃO DE IDIOMA
        top_bar = QHBoxLayout()
        top_bar.addStretch()
        self.btn_lang = QPushButton()
        self.btn_lang.setFixedWidth(120)
        self.btn_lang.clicked.connect(self.toggle_language)
        top_bar.addWidget(self.btn_lang)
        layout.addLayout(top_bar)

        # 1. HEADER
        self.group_config = QGroupBox("Config")
        h_layout = QHBoxLayout()
        self.lbl_name = QLabel("Nome:")
        h_layout.addWidget(self.lbl_name)
        self.txt_name = QLineEdit("my_wave")
        self.txt_name.textChanged.connect(self.update_code)
        h_layout.addWidget(self.txt_name)
        self.lbl_size = QLabel("Size:")
        h_layout.addWidget(self.lbl_size)
        self.cb_size = QComboBox()
        self.cb_size.addItems(["256", "512", "1024", "2048"])
        self.cb_size.currentIndexChanged.connect(self.change_size)
        h_layout.addWidget(self.cb_size)
        self.lbl_res = QLabel("Res:")
        h_layout.addWidget(self.lbl_res)
        self.cb_bits = QComboBox()
        self.cb_bits.addItems(["8-bit", "16-bit", "4-bit"])
        self.cb_bits.currentIndexChanged.connect(self.change_bits)
        h_layout.addWidget(self.cb_bits)
        self.group_config.setLayout(h_layout)
        layout.addWidget(self.group_config)

        # 2. CORPO
        splitter = QSplitter(Qt.Orientation.Horizontal)
        
        # --- PAINEL ESQUERDO (FERRAMENTAS) ---
        self.tabs = QTabWidget()
        # Define largura fixa para painel esquerdo, permitindo o direito expandir
        self.tabs.setFixedWidth(420)
        
        # Tab 1: Fórmula
        tab_form = QWidget()
        l_form = QVBoxLayout(tab_form)
        self.lbl_math = QLabel("Math:")
        l_form.addWidget(self.lbl_math)
        self.txt_formula = QLineEdit("sin(x)")
        self.txt_formula.returnPressed.connect(lambda: self.generate_formula(self.txt_formula.text()))
        l_form.addWidget(self.txt_formula)
        
        btn_presets_form = QGridLayout()
        presets = [("Sine", "sin(x)"), ("Saw", "((x/pi+1)%2)-1"), 
                   ("Square", "sign(sin(x))"), ("Triangle", "abs((x/pi-1)%2-1)*2-1"),
                   ("Pulse 10%", "where((x%(2*pi))<(0.2*pi), 1, -1)"), ("Noise", "random(sz)*2-1")]
        for i, (name, func) in enumerate(presets):
            btn = QPushButton(name)
            btn.clicked.connect(lambda _, f=func: (self.txt_formula.setText(f), self.generate_formula(f)))
            btn_presets_form.addWidget(btn, i//2, i%2)
        l_form.addLayout(btn_presets_form)
        self.lbl_tip = QLabel("Tip")
        self.lbl_tip.setWordWrap(True)
        l_form.addWidget(self.lbl_tip)
        l_form.addStretch()
        self.tabs.addTab(tab_form, "Fórmula")
        
        # Tab 2: Aditiva (64 Harmônicos)
        tab_add = QWidget()
        l_add = QVBoxLayout(tab_add)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        w_scroll = QWidget()
        self.l_sliders = QVBoxLayout(w_scroll)
        self.sliders = []
        for i in range(64): 
            row = QHBoxLayout()
            lbl = QLabel(f"H{i+1}")
            lbl.setFixedWidth(35)
            row.addWidget(lbl)
            sl = QSlider(Qt.Orientation.Horizontal)
            sl.setRange(0, 100)
            sl.setValue(100 if i==0 else 0)
            sl.valueChanged.connect(self.generate_additive)
            row.addWidget(sl)
            self.sliders.append(sl)
            self.l_sliders.addLayout(row)
        scroll.setWidget(w_scroll)
        l_add.addWidget(scroll)
        self.btn_reset_add = QPushButton("Reset")
        self.btn_reset_add.clicked.connect(self.reset_additive)
        l_add.addWidget(self.btn_reset_add)
        self.tabs.addTab(tab_add, "Aditiva")

        # --- NOVA TAB 3: SÍNTESE FM ---
        tab_fm = QWidget()
        l_fm = QVBoxLayout(tab_fm)
        
        # Seleção de Algoritmo
        h_algo = QHBoxLayout()
        self.lbl_algo = QLabel("Algoritmo:")
        h_algo.addWidget(self.lbl_algo)
        self.cb_algo = QComboBox()
        self.algos_desc = [
            "1: Stack (4->3->2->1)",
            "2: 3 Mod 1, 4 Mod 2 (Pairs)",
            "3: 4->3->1, 2->1",
            "4: 4->3, 2, 1 (3 Carriers)",
            "5: 4->1, 3->1, 2->1 (Fan)",
            "6: 4->(3+2+1) (Broad)",
            "7: Additive (All Out)",
            "8: 4->3->2, 1 Out"
        ]
        self.cb_algo.addItems(self.algos_desc)
        self.cb_algo.currentIndexChanged.connect(self.generate_fm)
        h_algo.addWidget(self.cb_algo)
        l_fm.addLayout(h_algo)
        
        self.lbl_algo_visual = QLabel("Fluxo: 4 -> 3 -> 2 -> 1 -> OUT")
        self.lbl_algo_visual.setStyleSheet("color: #aaa; font-style: italic;")
        l_fm.addWidget(self.lbl_algo_visual)

        # Scroll para os 4 Operadores
        fm_scroll = QScrollArea()
        fm_scroll.setWidgetResizable(True)
        w_fm_scroll = QWidget()
        l_fm_ops = QVBoxLayout(w_fm_scroll)
        
        self.ops_widgets = []
        for i in range(4): # Operadores 1 a 4
            op = FMOperatorWidget(i + 1)
            op.valueChanged.connect(self.generate_fm)
            l_fm_ops.addWidget(op)
            self.ops_widgets.append(op)
        
        # Feedback Global (geralmente no Op 4)
        grp_fb = QGroupBox("Feedback (Op 4)")
        l_fb = QHBoxLayout(grp_fb)
        self.lbl_feedback = QLabel("Amt:")
        self.slider_feedback = QSlider(Qt.Orientation.Horizontal)
        self.slider_feedback.setRange(0, 100)
        self.slider_feedback.valueChanged.connect(self.generate_fm)
        l_fb.addWidget(self.lbl_feedback)
        l_fb.addWidget(self.slider_feedback)
        l_fm_ops.addWidget(grp_fb)

        fm_scroll.setWidget(w_fm_scroll)
        l_fm.addWidget(fm_scroll)
        
        self.tabs.addTab(tab_fm, "FM (4-Op)")
        
        splitter.addWidget(self.tabs)

        # --- PAINEL DIREITO (VISUALIZADOR E CÓDIGO) ---
        right_panel = QWidget()
        r_layout = QVBoxLayout(right_panel)
        r_layout.setContentsMargins(0,0,0,0)
        
        self.viz = WaveformDisplay()
        self.viz.dataEdited.connect(self.on_manual_draw)
        # Importante: Permite que o visualizador expanda
        r_layout.addWidget(self.viz, 3) 
        
        # Piano Controls
        piano_ctrl_layout = QHBoxLayout()
        self.lbl_piano = QLabel("Piano:")
        piano_ctrl_layout.addWidget(self.lbl_piano)
        piano_ctrl_layout.addStretch()
        self.lbl_octave = QLabel("Oct:")
        piano_ctrl_layout.addWidget(self.lbl_octave)
        self.spin_octave = QSpinBox()
        self.spin_octave.setRange(0, 8)
        self.spin_octave.setValue(4)
        self.spin_octave.valueChanged.connect(self.update_octave)
        piano_ctrl_layout.addWidget(self.spin_octave)
        r_layout.addLayout(piano_ctrl_layout)

        self.piano = PianoWidget()
        self.piano.set_octave(4)
        self.piano.noteOn.connect(self.play_preview)
        self.piano.noteOff.connect(self.audio.stop)
        r_layout.addWidget(self.piano)
        
        self.code_viewer = QPlainTextEdit()
        self.code_viewer.setFont(QFont("Consolas", 10))
        self.code_viewer.setReadOnly(True)
        # Código com fator de estiramento menor
        r_layout.addWidget(self.code_viewer, 1) 
        
        splitter.addWidget(right_panel)
        
        # Configura o splitter para dar prioridade ao painel direito
        splitter.setStretchFactor(0, 0) # Painel esquerdo fixo
        splitter.setStretchFactor(1, 1) # Painel direito expande
        
        layout.addWidget(splitter)

    # --- LÓGICA DE TRADUÇÃO ---
    def toggle_language(self):
        self.cur_lang = "en" if self.cur_lang == "pt" else "pt"
        self.update_texts()

    def update_texts(self):
        t = TRANSLATIONS[self.cur_lang]
        self.setWindowTitle(t["window_title"])
        self.group_config.setTitle(t["config_box"])
        self.lbl_name.setText(t["name_lbl"])
        self.lbl_size.setText(t["size_lbl"])
        self.lbl_res.setText(t["res_lbl"])
        
        idx = self.cb_bits.currentIndex()
        self.cb_bits.setItemText(0, t["bits_8"])
        self.cb_bits.setItemText(1, t["bits_16"])
        self.cb_bits.setItemText(2, t["bits_4"])
        self.cb_bits.setCurrentIndex(idx)
        
        self.tabs.setTabText(0, t["tab_formula"])
        self.tabs.setTabText(1, t["tab_additive"])
        self.tabs.setTabText(2, t["tab_fm"]) # FM
        self.lbl_math.setText(t["math_func"])
        self.lbl_tip.setText(t["tip_formula"])
        self.btn_reset_add.setText(t["reset_add"])
        
        self.lbl_piano.setText(t["piano_header"])
        self.lbl_octave.setText(t["octave_lbl"])
        self.btn_lang.setText(t["btn_lang"])
        
        # Traduz widgets FM
        self.lbl_algo.setText(t["fm_algo"])
        self.lbl_feedback.setText(t["fm_feedback"])
        for op in self.ops_widgets:
            op.lbl_ratio.setText(t["op_ratio"])
            op.lbl_level.setText(t["op_level"])
            op.lbl_detune.setText(t["op_detune"])

        self.update_code()

    # --- LÓGICA DE DADOS ---
    def process_data_to_bits(self, data):
        if self.current_bit_depth == 16:
            return data 
        elif self.current_bit_depth == 8:
            data_u8 = ((data + 1.0) * 127.5).clip(0, 255).astype(np.uint8)
            return (data_u8.astype(float) / 127.5) - 1.0
        elif self.current_bit_depth == 4:
            steps = 15.0
            data_u4 = ((data + 1.0) * (steps / 2.0)).clip(0, steps).astype(np.uint8)
            return (data_u4.astype(float) / (steps / 2.0)) - 1.0
        return data

    def update_viz_and_code(self):
        processed = self.process_data_to_bits(self.raw_data)
        self.viz.setData(self.raw_data, processed)
        self.update_code()

    def change_size(self):
        new_size = int(self.cb_size.currentText())
        old_data = self.raw_data
        old_x = np.linspace(0, 1, len(old_data))
        new_x = np.linspace(0, 1, new_size)
        self.raw_data = np.interp(new_x, old_x, old_data)
        self.table_size = new_size
        self.update_viz_and_code()

    def change_bits(self):
        idx = self.cb_bits.currentIndex()
        if idx == 0: self.current_bit_depth = 8
        elif idx == 1: self.current_bit_depth = 16
        elif idx == 2: self.current_bit_depth = 4
        self.update_viz_and_code()

    # --- GERADORES ---
    def generate_formula(self, formula):
        sz = self.table_size
        try:
            x = np.linspace(0, 2*np.pi, sz, endpoint=False)
            ctx = {
                "sin": np.sin, "cos": np.cos, "tan": np.tan, "tanh": np.tanh,
                "exp": np.exp, "log": np.log, "sqrt": np.sqrt, "abs": np.abs,
                "sign": np.sign, "pi": np.pi, "random": np.random.rand,
                "where": np.where,
                "x": x, "sz": sz
            }
            res = eval(formula, {"__builtins__": None}, ctx)
            if np.isscalar(res): res = np.full(sz, res)
            res = np.nan_to_num(res.astype(float))
            mx = np.max(np.abs(res))
            if mx > 0: res /= mx
            self.raw_data = res
            self.viz.draw_mode = "formula"
            self.update_viz_and_code()
        except Exception as e:
            msg = TRANSLATIONS[self.cur_lang]["formula_err"]
            self.code_viewer.setPlainText(f"{msg}{e}")

    def generate_additive(self):
        sz = self.table_size
        x = np.linspace(0, 2*np.pi, sz, endpoint=False)
        y = np.zeros(sz)
        for i, sl in enumerate(self.sliders):
            amp = sl.value() / 100.0
            if amp > 0: y += amp * np.sin((i+1) * x)
        mx = np.max(np.abs(y))
        if mx > 0: y /= mx
        self.raw_data = y
        self.viz.draw_mode = "additive"
        self.update_viz_and_code()
    
    def reset_additive(self):
        for i, sl in enumerate(self.sliders):
            sl.blockSignals(True)
            sl.setValue(100 if i==0 else 0)
            sl.blockSignals(False)
        self.generate_additive()

    # --- LÓGICA DE SÍNTESE FM ---
    def generate_fm(self):
        # Coleta parâmetros
        ops = [op.get_params() for op in self.ops_widgets]
        fb_amt = self.slider_feedback.value() / 100.0 * 3.0 # Escala feedback
        algo = self.cb_algo.currentIndex()
        sz = self.table_size
        x = np.linspace(0, 2*np.pi, sz, endpoint=False)
        
        # Função auxiliar de oscilador
        def osc(phase, ratio, detune):
            return np.sin(phase * (ratio + (detune * 0.01)))

        # Buffer para feedback (precisa de loop real ou aproximação)
        # Para wavetable estática, fazemos aproximação de modulação de fase
        
        # Calcula saída de cada operador
        # DX7 Phase Modulation: sin(Phase + ModInput)
        
        # Op 4 com feedback (Simulação simples: Sine + (Sine*Fb))
        # Para loop perfeito em feedback real, precisaria resolver sample a sample.
        # Aqui usamos vetorização: Feedback vira uma distorção de fase na própria onda
        # Op 4
        # Aproximação de Feedback: Modula a fase com a própria onda (simplificado)
        if fb_amt > 0:
            # Feedback real requer iteração, mas é lento em Python puro.
            # Vamos fazer uma iteração rápida
            o4 = np.zeros(sz)
            last = 0.0
            p4 = x * (ops[3]["ratio"] + ops[3]["detune"]*0.01)
            amp4 = ops[3]["level"]
            for i in range(sz):
                val = amp4 * math.sin(p4[i] + last * fb_amt)
                o4[i] = val
                last = val
        else:
            o4 = ops[3]["level"] * osc(x, ops[3]["ratio"], ops[3]["detune"])

        # Op 3
        def get_op(idx, mod_input=0):
            p = ops[idx]["level"]
            r = ops[idx]["ratio"]
            d = ops[idx]["detune"]
            # PM: sin(phase + mod)
            return p * np.sin(x * (r + d*0.01) + mod_input)

        # Roteamento baseado no Algoritmo (Simulando DX7 Algos)
        out = np.zeros(sz)
        
        # Textos visuais
        alg_txts = [
            "4 -> 3 -> 2 -> 1 -> OUT (Stack)",
            "3->1, 4->2 (Parallel Modulators)",
            "4->3->1, 2->1 (Branch)",
            "4->3, 4->2, 4->1 (1 Mod drives 3)", # Correção lógica
            "4->1, 3->1, 2->1 (3 Mods drive 1)",
            "4 -> (3,2,1) (Broad)",
            "1 + 2 + 3 + 4 (Additive)",
            "4->3->2, 1 (Separate)"
        ]
        self.lbl_algo_visual.setText(f"Fluxo: {alg_txts[algo]}")

        if algo == 0: # Stack: 4->3->2->1
            o3 = get_op(2, o4 * 5.0) # *5.0 para dar mais força à modulação FM
            o2 = get_op(1, o3 * 5.0)
            out = get_op(0, o2 * 5.0)
            
        elif algo == 1: # (4->3) + (2->1)
            o3 = get_op(2, o4 * 5.0)
            # Op 2 é carrier aqui? No DX7 algo 5: 2->1, 4->3. Vamos assumir 1 e 3 carriers.
            # Vamos fazer (4->2) e (3->1)
            o2 = get_op(1, o4 * 5.0)
            o3 = get_op(2, 0) # 3 Livre? Vamos fazer 3 mod 1
            out = get_op(0, o2 * 5.0 + o3 * 5.0) # Mistura
            
        elif algo == 2: # 4->3->1, 2->1
            o3 = get_op(2, o4 * 5.0)
            o2 = get_op(1, 0)
            out = get_op(0, o3 * 5.0 + o2 * 5.0)

        elif algo == 3: # 4->3, 2, 1 (Todos carriers modulados por 4?)
            # Vamos fazer 4 modulando 3, 2 e 1
            o3 = get_op(2, o4 * 5.0)
            o2 = get_op(1, o4 * 5.0)
            o1 = get_op(0, o4 * 5.0)
            out = (o1 + o2 + o3) / 3.0
            
        elif algo == 4: # 4->1, 3->1, 2->1
            # 3 moduladores em paralelo entrando no 1
            o3 = get_op(2, 0)
            o2 = get_op(1, 0)
            # o4 já calculado
            mods = (o4 + o3 + o2) * 3.0
            out = get_op(0, mods)
            
        elif algo == 5: # 4->(3,2,1)
             # Igual ao 3? Vamos mudar.
             # 4 -> 3 -> Out, 2 -> Out, 1 -> Out
             o3 = get_op(2, o4 * 5.0)
             o2 = get_op(1, 0)
             o1 = get_op(0, 0)
             out = (o3 + o2 + o1) / 3.0

        elif algo == 6: # Additive
            o3 = get_op(2, 0)
            o2 = get_op(1, 0)
            o1 = get_op(0, 0)
            out = (o4 + o3 + o2 + o1) / 4.0
            
        elif algo == 7: # 4->3->2, 1 isolado
             o3 = get_op(2, o4 * 5.0)
             o2 = get_op(1, o3 * 5.0)
             o1 = get_op(0, 0)
             out = (o2 + o1) / 2.0

        # Normaliza
        mx = np.max(np.abs(out))
        if mx > 0: out /= mx
        
        self.raw_data = out
        self.viz.draw_mode = "fm"
        self.update_viz_and_code()

    def on_manual_draw(self, data):
        self.raw_data = data
        self.viz.draw_mode = "manual"
        self.update_viz_and_code()

    def update_octave(self):
        self.piano.set_octave(self.spin_octave.value())

    def play_preview(self, note):
        freq = 440.0 * (2 ** ((note - 69) / 12.0))
        processed = self.process_data_to_bits(self.raw_data)
        self.audio.play_tone(freq, processed)

    def update_code(self):
        t = TRANSLATIONS[self.cur_lang]
        name = "".join(x for x in self.txt_name.text() if x.isalnum() or x=="_")
        if not name: name = "wavetable"
        
        processed = self.process_data_to_bits(self.raw_data)
        sz = len(processed)
        
        code = f"// Wavetable: {name}\n"
        code += f"// Size: {sz} samples | Bit Depth: {self.current_bit_depth}-bit\n"
        
        if self.current_bit_depth == 16:
            data_int = (processed * 32767).astype(np.int16)
            code += f"const int16_t wt_{name}[] = {{\n"
            step_type = "BITS_16"
        elif self.current_bit_depth == 8:
            data_int = ((processed + 1.0) * 127.5).clip(0, 255).astype(np.uint8)
            code += f"const uint8_t wt_{name}[] = {{\n"
            step_type = "BITS_8"
        elif self.current_bit_depth == 4:
            steps = 15.0
            data_quant = ((processed + 1.0) * (steps / 2.0)).clip(0, steps).astype(np.uint8)
            data_int = (data_quant * 17).astype(np.uint8) 
            code += f"const uint8_t wt_{name}[] = {{\n"
            step_type = "BITS_8" 
            code += t["code_note_4bit"]

        lines = []
        curr_line = "    "
        for i, val in enumerate(data_int):
            curr_line += f"{val}, "
            if (i + 1) % 16 == 0:
                lines.append(curr_line)
                curr_line = "    "
        if curr_line.strip(): lines.append(curr_line)
        
        code += "\n".join(lines)
        code += "\n};\n\n"
        
        code += t["code_setup"]
        code += f"// synth.setWavetable(0, wt_{name}, {sz}, {step_type});\n"
        
        self.code_viewer.setPlainText(code)

    def apply_theme(self):
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
        palette.setColor(QPalette.ColorRole.HighlightedText, Qt.GlobalColor.black)
        QApplication.setPalette(palette)
        QApplication.setStyle("Fusion")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())