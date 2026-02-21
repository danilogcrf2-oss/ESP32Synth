import sys
import numpy as np
import pygame
import math
import copy

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, 
    QPushButton, QLabel, QComboBox, QSpinBox, QPlainTextEdit, 
    QSplitter, QGroupBox, QStyle, QLineEdit, QDialog, QSlider, QScrollArea, 
    QFrame, QRadioButton, QButtonGroup, QTabWidget, QGridLayout, QSizePolicy, QDoubleSpinBox,
    QListWidget, QListWidgetItem, QCheckBox
)
from PyQt6.QtCore import Qt, pyqtSignal, QTimer, QPointF, QRectF, QRect
from PyQt6.QtGui import QPainter, QColor, QPen, QPalette, QFont, QBrush, QPainterPath

# --- DICIONÁRIO DE TRADUÇÃO ---
TRANSLATIONS = {
    "pt": {
        "window_title": "Criador de Wavetables para ESP32Synth (Ultimate Edition)",
        "config_box": "Configurações da Wavetable",
        "name_lbl": "Nome:",
        "size_lbl": "Tamanho:",
        "res_lbl": "Resolução:",
        "bits_8": "8-bit (Padrão)",
        "bits_16": "16-bit (Hi-Fi)",
        "bits_4": "4-bit (Glitch/Lo-Fi)",
        "tab_formula": "Fórmula",
        "tab_additive": "Aditiva",
        "tab_fm": "Síntese FM",
        "tab_filters": "Filtros & FX",
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
        # Filtros
        "filter_type": "Tipo de Efeito:",
        "filter_p1": "Parâmetro 1 (Cutoff/Drive):",
        "filter_p2": "Parâmetro 2 (Res/Mix):",
        "btn_apply": "Aplicar Efeito",
        "btn_undo": "Desfazer (Undo)",
        "fx_lp": "Low Pass (FFT)",
        "fx_hp": "High Pass (FFT)",
        "fx_bp": "Band Pass (FFT)",
        "fx_notch": "Notch (FFT)",
        "fx_fold": "Wave Folder (Distortion)",
        "fx_sat": "Saturation (Soft Clip)",
        "fx_redux": "Sample Rate Redux",
        "fx_norm": "Normalize (Max Vol)",
        # Morph Tab
        "tab_morph": "Morph / Animation",
        "morph_grp_capture": "1. Keyframe Capture",
        "morph_btn_capture": "📸 Capture Current Frame (Snapshot)",
        "morph_btn_up": "▲",
        "morph_btn_down": "▼",
        "morph_btn_dup": "Duplicate",
        "morph_btn_del": "Delete",
        "morph_btn_update": "Update Frame",
        "morph_grp_engine": "2. Animation Generation",
        "morph_lbl_anim_size": "Final Animation Frames:",
        "morph_btn_gen": "Generate Animation (Morph)",
        "morph_grp_preview": "3. Animation Preview",
        "morph_btn_play": "▶ Play",
        "morph_btn_pause": "❚❚ Pause",
        "morph_grp_export": "4. C++ Export Format",
        "morph_radio_block": "Big Block (um array gigante)",
        "morph_radio_seq": "Instrument Sequence (ponteiros)",
        "morph_chk_instrument": "Gerar struct Instrument completa",
    },
    "en": {
        "window_title": "Wavetable Maker for ESP32Synth (Ultimate Edition)",
        "config_box": "Wavetable Settings",
        "name_lbl": "Name:",
        "size_lbl": "Size:",
        "res_lbl": "Resolution:",
        "bits_8": "8-bit (Standard)",
        "bits_16": "16-bit (Hi-Fi)",
        "bits_4": "4-bit (Glitch/Lo-Fi)",
        "tab_formula": "Formula",
        "tab_additive": "Additive",
        "tab_fm": "FM Synthesis",
        "tab_filters": "Filters & FX",
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
        "filter_type": "Effect Type:",
        "filter_p1": "Parameter 1 (Cutoff/Drive):",
        "filter_p2": "Parameter 2 (Res/Mix):",
        "btn_apply": "Apply Effect",
        "btn_undo": "Undo",
        "fx_lp": "Low Pass (FFT)",
        "fx_hp": "High Pass (FFT)",
        "fx_bp": "Band Pass (FFT)",
        "fx_notch": "Notch (FFT)",
        "fx_fold": "Wave Folder (Distortion)",
        "fx_sat": "Saturation (Soft Clip)",
        "fx_redux": "Sample Rate Redux",
        "fx_norm": "Normalize (Max Vol)",
        # Morph Tab
        "tab_morph": "Morph / Animation",
        "morph_grp_capture": "1. Keyframe Capture",
        "morph_btn_capture": "📸 Capture Current Frame (Snapshot)",
        "morph_btn_up": "▲",
        "morph_btn_down": "▼",
        "morph_btn_dup": "Duplicate",
        "morph_btn_del": "Delete",
        "morph_btn_update": "Update Frame",
        "morph_grp_engine": "2. Animation Generation",
        "morph_lbl_anim_size": "Final Animation Frames:",
        "morph_btn_gen": "Generate Animation (Morph)",
        "morph_grp_preview": "3. Animation Preview",
        "morph_btn_play": "▶ Play",
        "morph_btn_pause": "❚❚ Pause",
        "morph_grp_export": "4. C++ Export Format",
        "morph_radio_block": "Big Block (one giant array)",
        "morph_radio_seq": "Instrument Sequence (pointers)",
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
        self.setMinimumHeight(250) 
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        
        self.source_data = np.zeros(256) 
        self.processed_data = np.zeros(256) 
        self.is_drawing = False
        self.draw_mode = "manual" 
        self.last_draw_pos = None 
        
        self.color_line = QColor("#00ff7f")
        self.color_fill = QColor(0, 255, 127, 40)
        self.setBackgroundRole(QPalette.ColorRole.Base)
        self.setAutoFillBackground(True)

    def setData(self, source, processed):
        self.source_data = source
        self.processed_data = processed
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
            self.dataEdited.emit(self.source_data)

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
        idx = int((pos.x() / w) * len(self.source_data))
        val = 1.0 - (pos.y() / (h / 2.0)) # Y invertido
        val = max(-1.0, min(val, 1.0))
        idx = max(0, min(idx, len(self.source_data) - 1))
        
        # Modifica o source e o processed para feedback visual imediato
        self.source_data[idx] = val
        self.processed_data[idx] = val 

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#252525"))
        
        painter.setPen(QPen(QColor("#444444"), 1, Qt.PenStyle.DotLine))
        mid_y = self.height() / 2
        painter.drawLine(0, int(mid_y), self.width(), int(mid_y))
        
        if len(self.processed_data) < 2: return

        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        path = QPainterPath()
        w = self.width()
        scale_y = (self.height() / 2) * 0.90 
        step_x = w / len(self.processed_data)

        start_y = mid_y - (self.processed_data[0] * scale_y)
        path.moveTo(0, start_y)
        
        for i in range(1, len(self.processed_data)):
            x = i * step_x
            y = mid_y - (self.processed_data[i] * scale_y)
            path.lineTo(x, y)

        path_fill = QPainterPath(path)
        path_fill.lineTo(w, mid_y)
        path_fill.lineTo(0, mid_y)
        path_fill.closeSubpath()
        painter.setBrush(QBrush(self.color_fill))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawPath(path_fill)

        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(QPen(self.color_line, 2))
        painter.drawPath(path)
        
        painter.setPen(QColor("#888"))
        painter.drawText(10, 20, f"Samples: {len(self.processed_data)}")

# --- WIDGET FM ---
class FMOperatorWidget(QGroupBox):
    valueChanged = pyqtSignal()
    
    def __init__(self, index, parent=None):
        super().__init__(f"Operator {index}", parent)
        self.layout = QGridLayout(self)
        self.layout.setContentsMargins(5, 5, 5, 5)
        self.layout.setSpacing(5)

        self.lbl_ratio = QLabel("Ratio")
        self.spin_ratio = QDoubleSpinBox()
        self.spin_ratio.setRange(0.0, 32.0)
        self.spin_ratio.setSingleStep(0.5)
        self.spin_ratio.setValue(1.0)
        self.spin_ratio.valueChanged.connect(self.valueChanged.emit)

        self.lbl_level = QLabel("Level")
        self.slider_level = QSlider(Qt.Orientation.Horizontal)
        self.slider_level.setRange(0, 100)
        self.slider_level.setValue(100 if index == 1 else 0) 
        self.slider_level.valueChanged.connect(self.valueChanged.emit)

        self.lbl_detune = QLabel("Detune")
        self.spin_detune = QDoubleSpinBox()
        self.spin_detune.setRange(-5.0, 5.0)
        self.spin_detune.setSingleStep(0.1)
        self.spin_detune.setValue(0.0)
        self.spin_detune.valueChanged.connect(self.valueChanged.emit)

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

# --- PIANO ---
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
class SliderGroup(QGroupBox):
    """Um grupo de widgets com Slider, SpinBoxes para range e um label."""
    valueChanged = pyqtSignal()

    def __init__(self, title="Parâmetro", min_val=0.0, max_val=1.0, initial_val=0.5, parent=None):
        super().__init__(title, parent)
        self.layout = QGridLayout(self)
        self.layout.setContentsMargins(5, 5, 5, 5)
        self.layout.setSpacing(5)

        self.slider = QSlider(Qt.Orientation.Horizontal)
        self.slider.setRange(0, 1000)
        self.slider.valueChanged.connect(self.valueChanged.emit)

        self.spin_min = QDoubleSpinBox()
        self.spin_min.setRange(-1000.0, 1000.0)
        self.spin_min.setSingleStep(0.1)
        self.spin_min.setDecimals(2)
        self.spin_min.setFixedWidth(70)
        self.spin_min.valueChanged.connect(self.update_slider_from_spins)

        self.spin_max = QDoubleSpinBox()
        self.spin_max.setRange(-1000.0, 1000.0)
        self.spin_max.setSingleStep(0.1)
        self.spin_max.setDecimals(2)
        self.spin_max.setFixedWidth(70)
        self.spin_max.valueChanged.connect(self.update_slider_from_spins)

        self.lbl_val = QLabel("0.00")
        self.lbl_val.setFixedWidth(40)
        self.lbl_val.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)

        self.layout.addWidget(QLabel("Min:"), 0, 0)
        self.layout.addWidget(self.spin_min, 0, 1)
        self.layout.addWidget(QLabel("Max:"), 0, 2)
        self.layout.addWidget(self.spin_max, 0, 3)
        self.layout.addWidget(self.slider, 1, 0, 1, 4)
        self.layout.addWidget(self.lbl_val, 0, 4)
        
        self.set_range(min_val, max_val)
        self.set_value(initial_val)
        
        self.slider.valueChanged.connect(self.update_label)

    def set_range(self, min_val, max_val):
        self.spin_min.setValue(min_val)
        self.spin_max.setValue(max_val)

    def set_value(self, val):
        min_val, max_val = self.spin_min.value(), self.spin_max.value()
        if max_val > min_val:
            slider_val = int(1000 * (val - min_val) / (max_val - min_val))
            self.slider.setValue(slider_val)

    def get_value(self):
        min_val, max_val = self.spin_min.value(), self.spin_max.value()
        return min_val + (self.slider.value() / 1000.0) * (max_val - min_val)

    def update_slider_from_spins(self):
        # Garante que min < max
        if self.spin_min.value() >= self.spin_max.value():
            self.spin_max.setValue(self.spin_min.value() + 0.01)
        self.valueChanged.emit()
    
    def update_label(self):
        self.lbl_val.setText(f"{self.get_value():.2f}")

    def set_title(self, title):
        self.setTitle(title)
        
# --- JANELA PRINCIPAL ---
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.cur_lang = "pt"
        self.setWindowTitle("Wavetable Maker")
        self.setMinimumSize(1200, 800)
        
        self.audio = AudioEngine()
        
        self.source_data = np.zeros(256)      # Onda original (float -1 a 1)
        self.processed_data = np.zeros(256)   # Onda com filtros (float -1 a 1)
        self.undo_stack = []                  # Histórico de source_data
        
        # --- NOVAS VARIÁVEIS PARA ANIMAÇÃO ---
        self.animation_keyframes = []         # Lista de tuples (nome, np.ndarray)
        self.animation_frames = []            # Lista de np.ndarray com todos os frames interpolados
        self.animation_timer = QTimer(self)   # Timer para playback
        self.animation_timer.setInterval(33)  # ~30 FPS
        # ------------------------------------

        self.current_bit_depth = 8 
        self.table_size = 256
        
        self.init_ui()
        self.apply_theme()
        self.update_texts() 
        
        self.generate_formula("sin(x)")
        # Conecta o botão de undo global
        self.btn_undo.clicked.connect(self.undo_last)


    def init_ui(self):
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        layout = QVBoxLayout(main_widget)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(10)

        # 0. BARRA SUPERIOR
        top_bar = QHBoxLayout()
        self.btn_undo = QPushButton("Desfazer")
        self.btn_undo.setFixedWidth(100)
        top_bar.addWidget(self.btn_undo)
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
            btn.clicked.connect(lambda _, f=func: self._set_formula_and_generate(f))
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

        # Tab 3: Síntese FM
        tab_fm = QWidget()
        l_fm = QVBoxLayout(tab_fm)
        h_algo = QHBoxLayout()
        self.lbl_algo = QLabel("Algoritmo:")
        h_algo.addWidget(self.lbl_algo)
        self.cb_algo = QComboBox()
        self.algos_desc = ["1: Stack", "2: Pairs", "3: Branch", "4: 3 Carriers", "5: Fan", "6: Broad", "7: Additive", "8: Separate"]
        self.cb_algo.addItems(self.algos_desc)
        self.cb_algo.currentIndexChanged.connect(self.generate_fm)
        h_algo.addWidget(self.cb_algo)
        l_fm.addLayout(h_algo)
        
        self.lbl_algo_visual = QLabel("Fluxo: 4 -> 3 -> 2 -> 1 -> OUT")
        self.lbl_algo_visual.setStyleSheet("color: #aaa; font-style: italic;")
        l_fm.addWidget(self.lbl_algo_visual)

        fm_scroll = QScrollArea()
        fm_scroll.setWidgetResizable(True)
        w_fm_scroll = QWidget()
        l_fm_ops = QVBoxLayout(w_fm_scroll)
        self.ops_widgets = []
        for i in range(4): 
            op = FMOperatorWidget(i + 1)
            op.valueChanged.connect(self.generate_fm)
            l_fm_ops.addWidget(op)
            self.ops_widgets.append(op)
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
        self.tabs.addTab(tab_fm, "FM")
        
        # --- TAB 4: FILTROS E EFEITOS (REFEITO) ---
        tab_filters = QWidget()
        l_filt = QVBoxLayout(tab_filters)
        
        self.lbl_filter_type = QLabel("Tipo de Efeito:")
        l_filt.addWidget(self.lbl_filter_type)
        self.cb_filter_type = QComboBox()
        self.filter_keys = ["lp", "hp", "bp", "notch", "fold", "sat", "redux", "norm"]
        self.cb_filter_type.addItems(["Low Pass", "High Pass", "Band Pass", "Notch", "Wave Folder", "Saturation", "Redux/Bitcrush", "Normalize"])
        self.cb_filter_type.currentIndexChanged.connect(self._on_filter_type_change)
        l_filt.addWidget(self.cb_filter_type)
        
        l_filt.addSpacing(10)

        self.sl_fp1 = SliderGroup("P1", 0.0, 1.0, 0.5)
        self.sl_fp1.valueChanged.connect(self._update_filters_and_viz)
        l_filt.addWidget(self.sl_fp1)
        
        self.sl_fp2 = SliderGroup("P2", 0.0, 1.0, 0.0)
        self.sl_fp2.valueChanged.connect(self._update_filters_and_viz)
        l_filt.addWidget(self.sl_fp2)
        
        self.sl_fp3 = SliderGroup("P3", 0.0, 1.0, 0.0)
        self.sl_fp3.valueChanged.connect(self._update_filters_and_viz)
        l_filt.addWidget(self.sl_fp3)
        
        l_filt.addStretch()
        self.tabs.addTab(tab_filters, "Filtros")

        # --- TAB 5: MORPH / ANIMATION ---
        tab_morph = QWidget()
        l_morph = QVBoxLayout(tab_morph)

        # 1. Workflow de "Snapshot"
        self.grp_snapshot = QGroupBox("1. Captura de Keyframes")
        l_snapshot = QVBoxLayout(self.grp_snapshot)
        self.btn_capture_frame = QPushButton("📸 Capturar Frame Atual (Snapshot)")
        l_snapshot.addWidget(self.btn_capture_frame)
        
        self.list_keyframes = QListWidget()
        self.list_keyframes.setDragDropMode(QListWidget.DragDropMode.InternalMove)
        l_snapshot.addWidget(self.list_keyframes)

        l_keyframe_btns = QHBoxLayout()
        self.btn_kf_up = QPushButton("▲")
        self.btn_kf_down = QPushButton("▼")
        self.btn_kf_dup = QPushButton("Duplicar")
        self.btn_kf_del = QPushButton("Excluir")
        self.btn_kf_update = QPushButton("Atualizar Frame")
        l_keyframe_btns.addWidget(self.btn_kf_up)
        l_keyframe_btns.addWidget(self.btn_kf_down)
        l_keyframe_btns.addStretch()
        l_keyframe_btns.addWidget(self.btn_kf_dup)
        l_keyframe_btns.addWidget(self.btn_kf_update)
        l_keyframe_btns.addWidget(self.btn_kf_del)
        l_snapshot.addLayout(l_keyframe_btns)
        l_morph.addWidget(self.grp_snapshot)

        # 2. O Motor de Animação
        self.grp_engine = QGroupBox("2. Geração da Animação")
        l_engine = QGridLayout(self.grp_engine)
        self.lbl_anim_size = QLabel("Frames da Animação Final:")
        self.spin_anim_size = QSpinBox()
        self.spin_anim_size.setRange(2, 8192)
        self.spin_anim_size.setValue(64)
        self.btn_generate_morph = QPushButton("Gerar Animação (Morph)")
        l_engine.addWidget(self.lbl_anim_size, 0, 0)
        l_engine.addWidget(self.spin_anim_size, 0, 1)
        l_engine.addWidget(self.btn_generate_morph, 1, 0, 1, 2)
        l_morph.addWidget(self.grp_engine)
        
        # 3. Visualização e Playback
        self.grp_preview = QGroupBox("3. Preview da Animação")
        l_preview = QGridLayout(self.grp_preview)
        self.slider_anim_preview = QSlider(Qt.Orientation.Horizontal)
        self.slider_anim_preview.setRange(0, 0)
        self.slider_anim_preview.setEnabled(False)
        self.btn_anim_play = QPushButton("▶ Play")
        self.btn_anim_play.setCheckable(True)
        self.btn_anim_play.setEnabled(False)
        self.lbl_anim_frame_count = QLabel("0 / 0")
        l_preview.addWidget(self.slider_anim_preview, 0, 0, 1, 2)
        l_preview.addWidget(self.lbl_anim_frame_count, 1, 0)
        l_preview.addWidget(self.btn_anim_play, 1, 1)
        l_morph.addWidget(self.grp_preview)

        # 4. Exportação
        self.grp_export = QGroupBox("4. Formato de Exportação C++")
        l_export = QVBoxLayout(self.grp_export)
        self.radio_export_block = QRadioButton("Big Block (um array gigante)")
        self.radio_export_seq = QRadioButton("Instrument Sequence (ponteiros)")
        self.radio_export_block.setChecked(True)
        self.check_generate_instrument = QCheckBox("Gerar struct Instrument completa")
        l_export.addWidget(self.radio_export_block)
        l_export.addWidget(self.radio_export_seq)
        l_export.addWidget(self.check_generate_instrument)
        l_morph.addWidget(self.grp_export)

        l_morph.addStretch()
        self.tabs.addTab(tab_morph, "Morph / Animação")

        splitter.addWidget(self.tabs)

        # --- PAINEL DIREITO ---
        right_panel = QWidget()
        r_layout = QVBoxLayout(right_panel)
        r_layout.setContentsMargins(0,0,0,0)
        
        self.viz = WaveformDisplay()
        self.viz.dataEdited.connect(self.on_manual_draw)
        r_layout.addWidget(self.viz, 3) 
        
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
        r_layout.addWidget(self.code_viewer, 1)
        
        splitter.addWidget(right_panel)
        splitter.setStretchFactor(0, 0) 
        splitter.setStretchFactor(1, 1) 
        layout.addWidget(splitter)
        
        self._on_filter_type_change() # Config inicial dos sliders

        # --- CONEXÕES DA ABA DE ANIMAÇÃO ---
        self.btn_capture_frame.clicked.connect(self.capture_keyframe)
        self.btn_kf_del.clicked.connect(self.delete_keyframe)
        self.btn_kf_update.clicked.connect(self.update_keyframe)
        self.btn_kf_dup.clicked.connect(self.duplicate_keyframe)
        self.btn_kf_up.clicked.connect(self.move_keyframe_up)
        self.btn_kf_down.clicked.connect(self.move_keyframe_down)

        self.btn_generate_morph.clicked.connect(self.generate_morph)
        self.slider_anim_preview.valueChanged.connect(self.preview_animation_frame)
        self.btn_anim_play.toggled.connect(self.toggle_animation_playback)
        self.animation_timer.timeout.connect(self.animation_step)
        
        # Atualiza o estado dos botões com base na seleção da lista
        self.list_keyframes.currentItemChanged.connect(self.update_keyframe_button_states)
        self.list_keyframes.model().rowsMoved.connect(self.reorder_keyframes_from_drag)
        self.update_keyframe_button_states()

    # --- LÓGICA DE TRADUÇÃO E UI ---
    def _set_formula_and_generate(self, f):
        self.txt_formula.setText(f)
        self.generate_formula(f)

    def _on_filter_type_change(self):
        key = self.filter_keys[self.cb_filter_type.currentIndex()]
        
        # Esconde todos os sliders
        self.sl_fp1.setVisible(True)
        self.sl_fp2.setVisible(True)
        self.sl_fp3.setVisible(True)

        if key in ["lp", "hp"]:
            self.sl_fp1.set_title("Cutoff Freq")
            self.sl_fp1.set_range(0.0, self.table_size / 2)
            self.sl_fp1.set_value(self.table_size / 4)
            
            self.sl_fp2.set_title("Q / Resonance")
            self.sl_fp2.set_range(1.0, 20.0)
            self.sl_fp2.set_value(1.0)
            
            self.sl_fp3.set_title("Slope")
            self.sl_fp3.set_range(1, 8)
            self.sl_fp3.set_value(1)

        elif key in ["bp", "notch"]:
            self.sl_fp1.set_title("Cutoff Freq")
            self.sl_fp1.set_range(0.0, self.table_size / 2)
            self.sl_fp1.set_value(self.table_size / 4)
            
            self.sl_fp2.set_title("Width")
            self.sl_fp2.set_range(0.1, self.table_size / 4)
            self.sl_fp2.set_value(10.0)
            
            self.sl_fp3.setVisible(False)
        elif key == "fold":
            self.sl_fp1.set_title("Drive")
            self.sl_fp1.set_range(1.0, 20.0)
            self.sl_fp1.set_value(5.0)

            self.sl_fp2.set_title("Mix")
            self.sl_fp2.set_range(0.0, 1.0)
            self.sl_fp2.set_value(1.0)
            
            self.sl_fp3.setVisible(False)
        elif key == "sat":
            self.sl_fp1.set_title("Drive")
            self.sl_fp1.set_range(1.0, 20.0)
            self.sl_fp1.set_value(3.0)

            self.sl_fp2.set_title("Mix")
            self.sl_fp2.set_range(0.0, 1.0)
            self.sl_fp2.set_value(1.0)

            self.sl_fp3.setVisible(False)
        elif key == "redux":
            self.sl_fp1.set_title("Sample Rate")
            self.sl_fp1.set_range(1, self.table_size / 2)
            self.sl_fp1.set_value(self.table_size / 4)
            
            self.sl_fp2.set_title("Bit Depth")
            self.sl_fp2.set_range(2, 16)
            self.sl_fp2.set_value(16)
            
            self.sl_fp3.setVisible(False)
        else: # norm
            self.sl_fp1.setVisible(False)
            self.sl_fp2.setVisible(False)
            self.sl_fp3.setVisible(False)
            
        self._update_filters_and_viz()


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
        self.tabs.setTabText(2, t["tab_fm"])
        self.tabs.setTabText(3, t["tab_filters"])
        self.tabs.setTabText(4, t["tab_morph"])
        
        # Textos da aba de Morph
        self.grp_snapshot.setTitle(t["morph_grp_capture"])
        self.btn_capture_frame.setText(t["morph_btn_capture"])
        self.btn_kf_dup.setText(t["morph_btn_dup"])
        self.btn_kf_del.setText(t["morph_btn_del"])
        self.btn_kf_update.setText(t["morph_btn_update"])
        self.grp_engine.setTitle(t["morph_grp_engine"])
        self.lbl_anim_size.setText(t["morph_lbl_anim_size"])
        self.btn_generate_morph.setText(t["morph_btn_gen"])
        self.grp_preview.setTitle(t["morph_grp_preview"])
        self.btn_anim_play.setText(t["morph_btn_play"] if not self.btn_anim_play.isChecked() else t["morph_btn_pause"])
        self.grp_export.setTitle(t["morph_grp_export"])
        self.radio_export_block.setText(t["morph_radio_block"])
        self.radio_export_seq.setText(t["morph_radio_seq"])
        self.check_generate_instrument.setText(t["morph_chk_instrument"])

        self.lbl_math.setText(t["math_func"])
        self.lbl_tip.setText(t["tip_formula"])
        self.btn_reset_add.setText(t["reset_add"])
        
        self.lbl_piano.setText(t["piano_header"])
        self.lbl_octave.setText(t["octave_lbl"])
        self.btn_lang.setText(t["btn_lang"])
        self.btn_undo.setText(t["btn_undo"])
        
        self.lbl_algo.setText(t["fm_algo"])
        self.lbl_feedback.setText(t["fm_feedback"])
        for op in self.ops_widgets:
            op.lbl_ratio.setText(t["op_ratio"])
            op.lbl_level.setText(t["op_level"])
            op.lbl_detune.setText(t["op_detune"])

        self.lbl_filter_type.setText(t["filter_type"])
        self._on_filter_type_change() # Atualiza nomes dos sliders

        idx_f = self.cb_filter_type.currentIndex()
        self.cb_filter_type.clear()
        self.cb_filter_type.addItems([
            t["fx_lp"], t["fx_hp"], t["fx_bp"], t["fx_notch"], 
            t["fx_fold"], t["fx_sat"], t["fx_redux"], t["fx_norm"]
        ])
        self.cb_filter_type.setCurrentIndex(idx_f)
        self.update_code()

    # --- LÓGICA DE DADOS E PROCESSAMENTO ---
    def _update_source_data(self, new_data):
        """Função central para atualizar a onda de origem."""
        self.push_undo()
        self.source_data = np.copy(new_data)
        self.viz.draw_mode = "manual" # Permitir desenho após gerar
        self._update_filters_and_viz()

    def _update_filters_and_viz(self):
        """Aplica filtros e atualiza a UI."""
        self._apply_filters()
        self.update_viz_and_code()

    def push_undo(self):
        if len(self.undo_stack) > 20: self.undo_stack.pop(0)
        self.undo_stack.append(np.copy(self.source_data))

    def undo_last(self):
        if self.undo_stack:
            self.source_data = self.undo_stack.pop()
            self._update_filters_and_viz()

    def _apply_filters(self):
        """Aplica o filtro selecionado ao self.source_data -> self.processed_data."""
        p1 = self.sl_fp1.get_value()
        p2 = self.sl_fp2.get_value()
        p3 = self.sl_fp3.get_value()
        
        key = self.filter_keys[self.cb_filter_type.currentIndex()]
        
        data = np.copy(self.source_data)
        sz = len(data)

        if key in ["lp", "hp", "bp", "notch"]:
            spectrum = np.fft.rfft(data)
            freqs = np.fft.rfftfreq(sz, 1/sz)
            mag = np.abs(spectrum)
            phase = np.angle(spectrum)
            
            cutoff = p1
            
            mask = np.ones_like(mag)
            
            if key == "lp":
                slope = int(p3)
                q = p2
                cutoff_idx = int(cutoff)
                # Cria uma transição suave
                transition_width = max(2, sz // 64) 
                x = np.linspace(-1, 1, transition_width)
                sigmoid = 1 / (1 + np.exp(slope * 5 * x))
                
                if cutoff_idx < len(mask):
                    start = max(0, cutoff_idx - transition_width//2)
                    end = min(len(mask), cutoff_idx + transition_width//2)
                    mask_len = end - start
                    if mask_len > 0:
                         mask[start:end] = np.interp(np.arange(mask_len), [0, mask_len-1], [1, 0])
                    mask[end:] = 0.0

                if q > 1.0 and cutoff_idx > 0 and cutoff_idx < len(mask):
                    res_width = max(1, int( (sz/2) / (cutoff * q * 0.5) ) )
                    res_start = max(0, cutoff_idx - res_width)
                    res_end = min(len(mask), cutoff_idx + 1)
                    mask[res_start:res_end] = np.linspace(1.0, q, res_end - res_start)

                if slope > 1:
                    mask = np.power(mask, slope)

            elif key == "hp":
                slope = int(p3)
                q = p2
                cutoff_idx = int(cutoff)
                
                transition_width = max(2, sz // 64) 
                x = np.linspace(-1, 1, transition_width)
                sigmoid = 1 / (1 + np.exp(-slope * 5 * x))

                if cutoff_idx < len(mask):
                    start = max(0, cutoff_idx - transition_width//2)
                    end = min(len(mask), cutoff_idx + transition_width//2)
                    mask_len = end - start
                    if mask_len > 0:
                        mask[start:end] = np.interp(np.arange(mask_len), [0, mask_len-1], [0, 1])
                    mask[:start] = 0.0

                if q > 1.0 and cutoff_idx < len(mask):
                    res_width = max(1, int( (sz/2) / (cutoff * q * 0.5) ))
                    res_start = cutoff_idx
                    res_end = min(len(mask), cutoff_idx + res_width)
                    if res_end > res_start:
                      mask[res_start:res_end] = np.linspace(q, 1.0, res_end - res_start)

                if slope > 1:
                    mask = np.power(mask, slope)

            elif key == "bp":
                width = p2
                low = max(0, int(cutoff - width/2))
                high = int(cutoff + width/2)
                mask[:low] = 0.0
                mask[high:] = 0.0
                mask[low:high] *= (1.0 + p2*0.1) # Usa P2 como 'ressonância' de ganho
            elif key == "notch":
                width = p2
                low = max(0, int(cutoff - width/2))
                high = min(len(mask), int(cutoff + width/2))
                mask[low:high] = 0.0

            new_mag = mag * mask
            new_spectrum = new_mag * np.exp(1j * phase)
            data = np.fft.irfft(new_spectrum, n=sz)

        elif key == "fold":
            drive = p1
            mix = p2
            folded = np.sin(data * drive)
            data = (1.0 - mix) * data + mix * folded

        elif key == "sat":
            drive = p1
            mix = p2
            saturated = np.tanh(data * drive)
            # Normaliza o sinal saturado antes de mixar para evitar queda de volume
            mx = np.max(np.abs(saturated))
            if mx > 0: saturated /= mx
            data = (1.0 - mix) * data + mix * saturated
            
        elif key == "redux":
            # Sample rate reduction
            steps = int(p1)
            for i in range(0, sz, steps):
                val = data[i]
                end = min(i + steps, sz)
                data[i:end] = val
            
            # Bit depth reduction
            bits = int(p2)
            if bits < 16:
                levels = 2 ** bits
                data = np.round(data * (levels / 2)) / (levels / 2)

        if key != "norm":
            mx = np.max(np.abs(data))
            if mx > 0: data /= mx
        else: # Normalize
            mx = np.max(np.abs(data))
            if mx > 0: data /= mx
        
        self.processed_data = data

    def process_data_to_quantized_display(self, data):
        """Retorna uma versão quantizada para exibição, mas ainda float."""
        if self.current_bit_depth == 16:
            return data 
        elif self.current_bit_depth == 8:
            data_u8 = ((data + 1.0) * 127.5).clip(0, 255).astype(np.uint8)
            return (data_u8.astype(float) / 127.5) - 1.0
        elif self.current_bit_depth == 4:
            steps = 15.0
            data_u4 = np.round(((data + 1.0) / 2.0) * steps)
            return (data_u4 / (steps / 2.0)) - 1.0
        return data

    def update_viz_and_code(self):
        display_data = self.process_data_to_quantized_display(self.processed_data)
        self.viz.setData(self.source_data, display_data)
        self.update_code()

    def change_size(self):
        new_size = int(self.cb_size.currentText())
        if new_size == self.table_size: return

        old_x = np.linspace(0, 1, self.table_size)
        new_x = np.linspace(0, 1, new_size)
        
        # Interpola ambos os buffers
        new_source = np.interp(new_x, old_x, self.source_data)
        
        self.table_size = new_size
        self._update_source_data(new_source)
        self._on_filter_type_change() # Atualiza ranges dos sliders

    def change_bits(self):
        idx = self.cb_bits.currentIndex()
        if idx == 0: self.current_bit_depth = 8
        elif idx == 1: self.current_bit_depth = 16
        else: self.current_bit_depth = 4
        self.update_viz_and_code()

    # --- GERADORES DE ONDA ---
    def generate_formula(self, formula):
        sz = self.table_size
        try:
            x = np.linspace(0, 2 * np.pi, sz, endpoint=False)
            ctx = { "np": np, "sin": np.sin, "cos": np.cos, "tan": np.tan, "tanh": np.tanh,
                    "exp": np.exp, "log": np.log, "sqrt": np.sqrt, "abs": np.abs,
                    "sign": np.sign, "pi": np.pi, "random": np.random.rand,
                    "where": np.where, "x": x, "sz": sz }
            res = eval(formula, {"__builtins__": None}, ctx)
            if np.isscalar(res): res = np.full(sz, res)
            res = np.nan_to_num(res.astype(float))
            mx = np.max(np.abs(res))
            if mx > 0: res /= mx
            self._update_source_data(res)
        except Exception as e:
            msg = TRANSLATIONS[self.cur_lang]["formula_err"]
            self.code_viewer.setPlainText(f"{msg}{e}")

    def generate_additive(self):
        sz = self.table_size
        x = np.linspace(0, 2 * np.pi, sz, endpoint=False)
        y = np.zeros(sz)
        for i, sl in enumerate(self.sliders):
            amp = sl.value() / 100.0
            if amp > 0: y += amp * np.sin((i + 1) * x)
        mx = np.max(np.abs(y))
        if mx > 0: y /= mx
        self._update_source_data(y)
    
    def reset_additive(self):
        for i, sl in enumerate(self.sliders):
            sl.blockSignals(True)
            sl.setValue(100 if i==0 else 0)
            sl.blockSignals(False)
        self.generate_additive()

    def generate_fm(self):
        ops = [op.get_params() for op in self.ops_widgets]
        fb_amt = self.slider_feedback.value() / 100.0 * 3.0 
        algo = self.cb_algo.currentIndex()
        sz = self.table_size
        x = np.linspace(0, 2*np.pi, sz, endpoint=False)
        
        o4 = np.zeros(sz)
        if fb_amt > 0:
            last = 0.0
            p4 = x * (ops[3]["ratio"] + ops[3]["detune"]*0.01)
            amp4 = ops[3]["level"]
            for i in range(sz):
                val = amp4 * math.sin(p4[i] + last * fb_amt)
                o4[i] = val
                last = val
        else:
            o4 = ops[3]["level"] * np.sin(x * (ops[3]["ratio"] + ops[3]["detune"]*0.01))

        def get_op(idx, mod_input=0):
            p = ops[idx]
            return p["level"] * np.sin(x * (p["ratio"] + p["detune"]*0.01) + mod_input)

        out = np.zeros(sz)
        alg_txts = ["4->3->2->1", "3->1, 4->2", "4->3->1, 2->1", "4->(3,2,1)", "4,3,2->1", "(4,3,2)+1", "1+2+3+4", "4->3->2, 1"]
        self.lbl_algo_visual.setText(f"Fluxo: {alg_txts[algo]}")

        mod_scale = 5.0
        if algo == 0: out = get_op(0, mod_scale * get_op(1, mod_scale * get_op(2, mod_scale * o4)))
        elif algo == 1: out = get_op(0, mod_scale * get_op(2, mod_scale * o4)) + get_op(1, mod_scale * o4)
        elif algo == 2: out = get_op(0, mod_scale * (get_op(2, mod_scale * o4) + get_op(1)))
        elif algo == 3: out = get_op(0, mod_scale * o4) + get_op(1, mod_scale * o4) + get_op(2, mod_scale * o4)
        elif algo == 4: out = get_op(0, mod_scale * (get_op(1) + get_op(2) + o4))
        elif algo == 5: out = get_op(0) + get_op(1) + get_op(2, mod_scale * o4)
        elif algo == 6: out = get_op(0) + get_op(1) + get_op(2) + o4
        elif algo == 7: out = get_op(0) + get_op(1, mod_scale * get_op(2, mod_scale * o4))

        mx = np.max(np.abs(out))
        if mx > 0: out /= mx
        self._update_source_data(out)

    def on_manual_draw(self, edited_data):
        self._update_source_data(edited_data)

    def update_octave(self):
        self.piano.set_octave(self.spin_octave.value())

    def play_preview(self, note):
        freq = 440.0 * (2 ** ((note - 69) / 12.0))
        display_data = self.process_data_to_quantized_display(self.processed_data)
        self.audio.play_tone(freq, display_data)

    def update_code(self):
        t = TRANSLATIONS[self.cur_lang]
        name = "".join(x for x in self.txt_name.text() if x.isalnum() or x=="_")
        if not name: name = "wavetable"

        # DECIDE SE VAMOS EXPORTAR UMA ANIMAÇÃO OU UMA ÚNICA ONDA
        is_anim_export = len(self.animation_frames) > 0 and self.tabs.tabText(self.tabs.currentIndex()).startswith("Morph")

        if not is_anim_export:
            # --- LÓGICA ORIGINAL PARA EXPORTAR UMA ÚNICA ONDA ---
            self.export_single_wavetable(name, self.processed_data, t)
        else:
            # --- NOVA LÓGICA PARA EXPORTAR ANIMAÇÃO ---
            if self.radio_export_block.isChecked():
                self.export_animation_big_block(name, t)
            else:
                self.export_animation_instrument_sequence(name, t)

    def quantize_frame(self, data_float, bit_depth):
        """Helper para quantizar um array float (-1 a 1) para o formato de inteiros desejado."""
        sz = len(data_float)
        
        if bit_depth == 16:
            return (data_float * 32767).clip(-32768, 32767).astype(np.int16)
        
        if bit_depth == 8:
            return ((data_float + 1.0) * 127.5).clip(0, 255).astype(np.uint8)

        if bit_depth == 4:
            data_4bit = np.round(((data_float + 1.0) / 2.0) * 15).clip(0, 15).astype(np.uint8)
            
            # Garante que o tamanho é par para empacotamento
            if sz % 2 != 0:
                data_4bit = np.append(data_4bit, data_4bit[-1])
                sz = len(data_4bit)

            packed_data = np.zeros(sz // 2, dtype=np.uint8)
            for i in range(0, sz, 2):
                packed_data[i//2] = (data_4bit[i] & 0x0F) | ((data_4bit[i+1] & 0x0F) << 4)
            return packed_data
        
        return np.array([]) # Retorno padrão

    def format_cpp_array(self, data_int, bit_depth):
        """Helper para formatar a lista de inteiros em uma string de array C++."""
        lines = []
        curr_line = "    "
        items_per_line = 16
        
        for i, val in enumerate(data_int):
            # Para 4-bit, os dados já estão empacotados em uint8_t, então formatamos como hex
            is_packed_4bit = bit_depth == 4
            curr_line += f"0x{val:02x}, " if is_packed_4bit else f"{val}, "
            
            if (i + 1) % items_per_line == 0:
                lines.append(curr_line)
                curr_line = "    "
        if curr_line.strip():
            lines.append(curr_line)
        return "\n".join(lines)

    def export_single_wavetable(self, name, data_to_export, t):
        """Gera o código C++ para uma única wavetable."""
        sz = len(data_to_export)
        bit_depth = self.current_bit_depth
        
        code = f"// Wavetable: {name}\n"
        code += f"// Size: {sz} samples | Bit Depth: {bit_depth}-bit\n"
        
        data_int = self.quantize_frame(data_to_export, bit_depth)
        
        cpp_type = "const int16_t" if bit_depth == 16 else "const uint8_t"
        step_type = f"BITS_{bit_depth}"
        
        if bit_depth == 4 and sz % 2 != 0:
             code += f"// Warning: Sample count padded to {sz+1} for 4-bit packing.\n"

        code += f"{cpp_type} wt_{name}[] = {{\n"
        code += self.format_cpp_array(data_int, bit_depth)
        code += "\n};\n\n"
        
        size_expr = f"sizeof(wt_{name})"
        if bit_depth == 16:
            size_expr += " / 2"
        elif bit_depth == 4:
            size_expr += " * 2"

        code += t["code_setup"]
        code += f"synth.setWavetable(0, wt_{name}, {size_expr}, {step_type});\n"
        
        self.code_viewer.setPlainText(code)

    def export_animation_big_block(self, name, t):
        """Gera o código C++ para uma animação em um único array."""
        bit_depth = self.current_bit_depth
        num_frames = len(self.animation_frames)
        table_size = len(self.animation_frames[0])
        total_samples = num_frames * table_size
        
        code = f"// Wavetable Animation: {name} (Big Block)\n"
        code += f"// {num_frames} frames | {table_size} samples/frame | {bit_depth}-bit\n"
        code += f"// Total samples: {total_samples}\n"
        
        # Concatena todos os frames quantizados
        all_data_int = []
        for frame_data in self.animation_frames:
            quantized = self.quantize_frame(frame_data, bit_depth)
            all_data_int.extend(quantized)

        cpp_type = "const int16_t" if bit_depth == 16 else "const uint8_t"
        step_type = f"BITS_{bit_depth}"
        
        code += f"{cpp_type} anim_{name}_block[] = {{\n"
        code += self.format_cpp_array(all_data_int, bit_depth)
        code += "\n};\n\n"
        
        code += t["code_setup"]
        code += "// Para usar este bloco, você precisará de uma função no ESP32Synth que\n"
        code += "// aceite um número de frames, ou controle o offset manualmente.\n"
        code += f"// Exemplo conceitual:\n"
        code += f"// synth.setWavetableAnimation(0, anim_{name}_block, {table_size}, {num_frames}, {step_type});\n"
        
        self.code_viewer.setPlainText(code)

    def export_animation_instrument_sequence(self, name, t):
        """Gera o código C++ como uma sequência de arrays + um array de ponteiros."""
        bit_depth = self.current_bit_depth
        num_frames = len(self.animation_frames)
        table_size = len(self.animation_frames[0])

        code = f"// Wavetable Animation: {name} (Instrument Sequence)\n"
        code += f"// {num_frames} frames | {table_size} samples/frame | {bit_depth}-bit\n\n"

        cpp_type = "const int16_t" if bit_depth == 16 else "const uint8_t"
        step_type = f"BITS_{bit_depth}"
        
        frame_names = []
        for i, frame_data in enumerate(self.animation_frames):
            frame_name = f"wt_{name}_anim_{i}"
            frame_names.append(frame_name)
            
            data_int = self.quantize_frame(frame_data, bit_depth)
            
            code += f"// Frame {i}\n"
            code += f"{cpp_type} {frame_name}[] = {{\n"
            code += self.format_cpp_array(data_int, bit_depth)
            code += "\n};\n\n"

        # Array de ponteiros
        code += f"// Array de ponteiros para a sequência de animação\n"
        code += f"{cpp_type}* seq_{name}[] = {{\n    "
        code += ", ".join(frame_names)
        code += "\n};\n\n"
        
        size_expr = f"{table_size}"

        code += t["code_setup"]
        
        if self.check_generate_instrument.isChecked():
            code += f"// Definição da struct e função de setup para o instrumento '{name}'\n"
            code += f"Instrument inst_{name};\n\n"
            code += f"void setup_{name}(Synth& synth, int instrument_slot) {{\n"
            code += f"    inst_{name}.wavetable = seq_{name}[0];\n"
            code += f"    inst_{name}.wavetableSize = {size_expr};\n"
            code += f"    inst_{name}.wavetableBitDepth = {step_type};\n"
            code += f"    inst_{name}.sequenceWavetables = seq_{name};\n"
            code += f"    inst_{name}.sequenceLength = {num_frames};\n"
            code += f"    // Aumente o valor de 'morph_time' para uma animação mais lenta\n"
            code += f"    inst_{name}.morph_time = 1.0f / {num_frames};\n"
            code += f"    synth.loadInstrument(instrument_slot, inst_{name});\n"
            code += f"}}\n\n"
            code += f"// Chame no seu setup() principal:\n"
            code += f"// setup_{name}(synth, 0);\n"
        else:
            code += "// Use com a struct Instrument do ESP32Synth:\n"
            code += f"// Instrument myInstrument;\n"
            code += f"// myInstrument.wavetable = seq_{name}[0]; // Frame inicial\n"
            code += f"// myInstrument.wavetableSize = {size_expr};\n"
            code += f"// myInstrument.wavetableBitDepth = {step_type};\n"
            code += f"// myInstrument.sequenceWavetables = seq_{name};\n"
            code += f"// myInstrument.sequenceLength = {num_frames};\n"
            code += f"// synth.loadInstrument(0, myInstrument);\n"

        self.code_viewer.setPlainText(code)

    # --- NOVA LÓGICA DE MORPH / ANIMAÇÃO ---
    def update_keyframe_button_states(self):
        """Ativa/desativa botões de manipulação de keyframe com base na seleção."""
        has_selection = self.list_keyframes.currentItem() is not None
        self.btn_kf_del.setEnabled(has_selection)
        self.btn_kf_update.setEnabled(has_selection)
        self.btn_kf_dup.setEnabled(has_selection)
        
        row = self.list_keyframes.currentRow()
        self.btn_kf_up.setEnabled(has_selection and row > 0)
        self.btn_kf_down.setEnabled(has_selection and row < self.list_keyframes.count() - 1)

    def capture_keyframe(self):
        """Captura o estado atual da onda processada como um novo keyframe."""
        # Armazena uma cópia dos dados da onda
        new_frame_data = np.copy(self.processed_data)
        frame_name = f"Keyframe {self.list_keyframes.count() + 1}"
        
        # Adiciona o array de dados na nossa lista de backend
        self.animation_keyframes.append(new_frame_data)
        
        # Adiciona um item na QListWidget para o usuário ver
        item = QListWidgetItem(frame_name)
        self.list_keyframes.addItem(item)
        self.list_keyframes.setCurrentItem(item)
        self.update_keyframe_button_states()

    def delete_keyframe(self):
        """Exclui o keyframe selecionado."""
        current_item = self.list_keyframes.currentItem()
        if not current_item: return
        
        row = self.list_keyframes.row(current_item)
        
        # Remove da QListWidget e da nossa lista de backend
        self.list_keyframes.takeItem(row)
        if row < len(self.animation_keyframes):
            del self.animation_keyframes[row]
            
        self.update_keyframe_button_states()

    def update_keyframe(self):
        """Atualiza o keyframe selecionado com a onda atualmente exibida."""
        current_item = self.list_keyframes.currentItem()
        if not current_item: return

        row = self.list_keyframes.row(current_item)
        if row < len(self.animation_keyframes):
            self.animation_keyframes[row] = np.copy(self.processed_data)
            # Poderíamos adicionar um feedback visual, como um "(atualizado!)" no nome, mas não é essencial
            
    def duplicate_keyframe(self):
        """Duplica o keyframe selecionado."""
        current_item = self.list_keyframes.currentItem()
        if not current_item: return

        row = self.list_keyframes.row(current_item)
        if row < len(self.animation_keyframes):
            original_data = self.animation_keyframes[row]
            new_data = np.copy(original_data)
            new_name = f"{current_item.text()} (Cópia)"
            
            # Insere o novo frame logo após o original
            self.animation_keyframes.insert(row + 1, new_data)
            self.list_keyframes.insertItem(row + 1, new_name)
            self.list_keyframes.setCurrentRow(row + 1)

    def move_keyframe_up(self):
        """Move o keyframe selecionado uma posição para cima."""
        row = self.list_keyframes.currentRow()
        if row > 0:
            # Move na QListWidget
            item = self.list_keyframes.takeItem(row)
            self.list_keyframes.insertItem(row - 1, item)
            self.list_keyframes.setCurrentRow(row - 1)
            
            # Move na lista de backend
            frame = self.animation_keyframes.pop(row)
            self.animation_keyframes.insert(row - 1, frame)
            
    def move_keyframe_down(self):
        """Move o keyframe selecionado uma posição para baixo."""
        row = self.list_keyframes.currentRow()
        if 0 <= row < self.list_keyframes.count() - 1:
            # Move na QListWidget
            item = self.list_keyframes.takeItem(row)
            self.list_keyframes.insertItem(row + 1, item)
            self.list_keyframes.setCurrentRow(row + 1)

            # Move na lista de backend
            frame = self.animation_keyframes.pop(row)
            self.animation_keyframes.insert(row + 1, frame)

    def reorder_keyframes_from_drag(self, source_parent, source_start, source_end, dest_parent, dest_row):
        """Sincroniza a lista de backend quando o usuário arrasta e solta um item."""
        if source_start != dest_row:
             # Remove o item da posição antiga e insere na nova
            moved_item = self.animation_keyframes.pop(source_start)
            if dest_row > source_start:
                 self.animation_keyframes.insert(dest_row -1, moved_item)
            else:
                 self.animation_keyframes.insert(dest_row, moved_item)

    def generate_morph(self):
        """Gera a sequência de animação interpolando os keyframes."""
        if len(self.animation_keyframes) < 2:
            # Idealmente, mostraríamos um QMessageBox aqui.
            print("Erro: São necessários pelo menos 2 keyframes para gerar a animação.")
            return

        total_anim_frames = self.spin_anim_size.value()
        num_keyframes = len(self.animation_keyframes)
        
        self.animation_frames = []
        
        # O np.linspace precisa de `num` pontos, então para o último keyframe, queremos que ele seja o ponto final
        x_points = np.linspace(0, num_keyframes - 1, total_anim_frames)
        
        # Os "índices" dos nossos keyframes (0, 1, 2, ...)
        xp = np.arange(num_keyframes)
        
        # Empilha os keyframes para que o numpy possa interpolar ao longo de um novo eixo
        # Shape: (num_keyframes, table_size)
        fp = np.stack(self.animation_keyframes)

        # Interpola todos os samples da wavetable de uma vez
        # np.interp requer que os pontos de interpolação `x_points` sejam 1D,
        # e os valores `fp` também sejam 1D. Precisamos fazer isso para cada sample da tabela.
        # Transpomos para que a interpolação ocorra ao longo do eixo dos keyframes.
        # Shape de fp.T: (table_size, num_keyframes)
        interpolated_stack = np.zeros((self.table_size, total_anim_frames))
        for i in range(self.table_size):
            interpolated_stack[i, :] = np.interp(x_points, xp, fp[:, i])
            
        # Agora, destranspomos para ter nossa lista de frames
        # Shape final: (total_anim_frames, table_size)
        final_frames = interpolated_stack.T

        # Normaliza cada frame individualmente para evitar clipping
        for frame in final_frames:
            mx = np.max(np.abs(frame))
            if mx > 0:
                frame /= mx
            self.animation_frames.append(frame)

        # Atualiza a UI de preview
        self.slider_anim_preview.setEnabled(True)
        self.slider_anim_preview.setRange(0, total_anim_frames - 1)
        self.btn_anim_play.setEnabled(True)
        self.lbl_anim_frame_count.setText(f"1 / {total_anim_frames}")
        self.update_code() # Gera o código para a nova animação
        
    def preview_animation_frame(self, frame_index):
        """Exibe um único frame da animação no visualizador principal."""
        if 0 <= frame_index < len(self.animation_frames):
            frame_data = self.animation_frames[frame_index]
            
            # Usamos o `source_data` do primeiro keyframe como um "fundo" de referência
            ref_source = self.animation_keyframes[0] if self.animation_keyframes else self.source_data
            
            display_data = self.process_data_to_quantized_display(frame_data)
            self.viz.setData(ref_source, display_data)
            
            total_frames = len(self.animation_frames)
            self.lbl_anim_frame_count.setText(f"{frame_index + 1} / {total_frames}")

    def toggle_animation_playback(self, checked):
        """Inicia ou para o playback da animação."""
        if checked and self.animation_frames:
            self.btn_anim_play.setText("❚❚ Pause")
            self.animation_timer.start()
        else:
            self.btn_anim_play.setText("▶ Play")
            self.animation_timer.stop()

    def animation_step(self):
        """Avança um frame na animação, chamado pelo QTimer."""
        if not self.animation_frames:
            self.toggle_animation_playback(False)
            return
            
        current_val = self.slider_anim_preview.value()
        max_val = self.slider_anim_preview.maximum()
        
        next_val = (current_val + 1) % (max_val + 1)
        self.slider_anim_preview.setValue(next_val)

    # --- FIM DA LÓGICA DE ANIMAÇÃO ---

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