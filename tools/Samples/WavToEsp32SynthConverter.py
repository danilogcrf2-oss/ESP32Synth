import sys
import os
import time
import numpy as np
import soundfile as sf
import pygame

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, 
    QPushButton, QLabel, QFileDialog, QComboBox, QSpinBox, 
    QPlainTextEdit, QSplitter, QGroupBox, QStyle
)
from PyQt6.QtCore import Qt, pyqtSignal, QRectF, QTimer
from PyQt6.QtGui import QPainter, QColor, QPen, QBrush, QPalette, QFont

# --- WIDGET CUSTOMIZADO PARA DESENHAR A ONDA ---
class WaveformWidget(QWidget):
    loopPointsChanged = pyqtSignal(int, int)

    def __init__(self):
        super().__init__()
        self.setMinimumHeight(150)
        self.setBackgroundRole(QPalette.ColorRole.Base)
        self.setAutoFillBackground(True)
        
        self.audio_data = None
        self.display_data = None 
        self.total_samples = 0
        
        self.loop_enabled = False
        self.loop_start = 0
        self.loop_end = 0
        
        self.playback_cursor = -1 # Posição do cursor (-1 = invisível)
        
        self.dragging = None 
        self.last_mouse_x = 0

    def setData(self, audio_data):
        # Garante mono para visualização
        if len(audio_data.shape) > 1:
            self.audio_data = np.mean(audio_data, axis=1)
        else:
            self.audio_data = audio_data
            
        self.total_samples = len(self.audio_data)
        self.loop_start = 0
        self.loop_end = self.total_samples - 1
        self.playback_cursor = -1
        self.resampleForDisplay()
        self.update()

    def resampleForDisplay(self):
        if self.audio_data is None or self.total_samples == 0: return
        width = self.width()
        if width <= 0: return
        factor = int(self.total_samples / width)
        if factor < 1: factor = 1
        # Otimização: Pega o pico absoluto para desenhar a forma de onda
        reduced_data = np.abs(self.audio_data[:len(self.audio_data)//factor*factor].reshape(-1, factor)).max(axis=1)
        self.display_data = reduced_data

    def setLoopEnabled(self, enabled):
        self.loop_enabled = enabled
        self.update()

    def setLoopPoints(self, start, end):
        self.loop_start = max(0, min(start, self.total_samples - 1))
        self.loop_end = max(self.loop_start + 1, min(end, self.total_samples))
        self.update()

    def setCursorPos(self, sample_idx):
        self.playback_cursor = sample_idx
        self.update()

    def sampleToPixel(self, sample_idx):
        if self.total_samples == 0: return 0
        return (sample_idx / self.total_samples) * self.width()

    def pixelToSample(self, pixel_x):
        if self.width() == 0: return 0
        val = int((pixel_x / self.width()) * self.total_samples)
        return max(0, min(val, self.total_samples - 1))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.fillRect(self.rect(), QColor("#2b2b2b"))

        if self.display_data is None:
            painter.setPen(QColor("#555555"))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "Carregue um arquivo WAV...")
            return

        mid_y = self.height() / 2
        scale_y = self.height() / 2 * 0.95 

        # 1. Desenha a Onda
        painter.setPen(QPen(QColor("#00ff7f"), 1)) 
        x_step = self.width() / len(self.display_data)
        for i, val in enumerate(self.display_data):
            x = i * x_step
            h = val * scale_y
            painter.drawLine(int(x), int(mid_y - h), int(x), int(mid_y + h))
            
        painter.setPen(QPen(QColor("#444444"), 1))
        painter.drawLine(0, int(mid_y), self.width(), int(mid_y)) 

        # 2. Desenha o Loop (Área Laranja)
        if self.loop_enabled:
            px_start = self.sampleToPixel(self.loop_start)
            px_end = self.sampleToPixel(self.loop_end)
            
            loop_rect = QRectF(px_start, 0, px_end - px_start, self.height())
            painter.fillRect(loop_rect, QBrush(QColor(255, 165, 0, 80))) 

            pen = QPen(QColor("#FFA500"), 2)
            painter.setPen(pen)
            painter.drawLine(int(px_start), 0, int(px_start), self.height())
            painter.drawLine(int(px_end), 0, int(px_end), self.height())
            
            # Handles (Triângulos)
            handle_size = 8
            painter.setBrush(QColor("#FFA500"))
            # Start Handle
            painter.drawPolygon([
                 QRectF(px_start - handle_size/2, 0, handle_size, handle_size).bottomLeft(),
                 QRectF(px_start - handle_size/2, 0, handle_size, handle_size).topRight(),
                 QRectF(px_start - handle_size/2, 0, handle_size, handle_size).topLeft(),
            ])
            # End Handle
            painter.drawPolygon([
                 QRectF(px_end - handle_size/2, 0, handle_size, handle_size).bottomRight(),
                 QRectF(px_end - handle_size/2, 0, handle_size, handle_size).topRight(),
                 QRectF(px_end - handle_size/2, 0, handle_size, handle_size).topLeft(),
            ])

        # 3. Desenha o Cursor de Playback (Linha Vermelha)
        if self.playback_cursor >= 0 and self.playback_cursor < self.total_samples:
            px_cursor = self.sampleToPixel(self.playback_cursor)
            painter.setPen(QPen(QColor("#FF3333"), 2))
            painter.drawLine(int(px_cursor), 0, int(px_cursor), self.height())

    def resizeEvent(self, event):
        self.resampleForDisplay()
        super().resizeEvent(event)

    def mousePressEvent(self, event):
        if not self.loop_enabled or self.total_samples == 0: return
        x = event.pos().x()
        sample_clicked = self.pixelToSample(x)
        threshold_px = 10 
        threshold_samples = (threshold_px / self.width()) * self.total_samples
        
        if abs(sample_clicked - self.loop_start) < threshold_samples:
            self.dragging = 'start'
        elif abs(sample_clicked - self.loop_end) < threshold_samples:
            self.dragging = 'end'
        elif self.loop_start < sample_clicked < self.loop_end:
            self.dragging = 'range' 
        else:
            self.dragging = None
        self.last_mouse_x = x

    def mouseMoveEvent(self, event):
        if not self.loop_enabled or self.dragging is None: return
        x = event.pos().x()
        dx_px = x - self.last_mouse_x
        dx_samples = int((dx_px / self.width()) * self.total_samples)
        
        if self.dragging == 'start':
            new_start = self.loop_start + dx_samples
            self.loop_start = max(0, min(new_start, self.loop_end - 100)) 
        elif self.dragging == 'end':
            new_end = self.loop_end + dx_samples
            self.loop_end = max(self.loop_start + 100, min(new_end, self.total_samples))
        elif self.dragging == 'range':
            new_start = self.loop_start + dx_samples
            new_end = self.loop_end + dx_samples
            if new_start >= 0 and new_end <= self.total_samples:
                self.loop_start = new_start
                self.loop_end = new_end

        self.last_mouse_x = x
        self.update()
        self.loopPointsChanged.emit(int(self.loop_start), int(int(self.loop_end)))

    def mouseReleaseEvent(self, event):
        self.dragging = None

# --- JANELA PRINCIPAL ---
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Conversor de Samples para ESP32Synth (v3.0)")
        self.setMinimumSize(900, 750)
        
        self.filepath = ""
        self.filename = "sample"
        self.sr = 44100
        self.raw_data_int16 = None
        self.raw_data_int16_mono = None
        self.current_sound = None 
        
        # Variáveis de Playback
        self.playback_start_time = 0
        self.is_playing = False
        
        # Timer para atualizar o visualizador
        self.timer = QTimer()
        self.timer.setInterval(30) # ~30 FPS
        self.timer.timeout.connect(self.update_playback_cursor)

        self.init_ui()
        self.apply_dark_theme() 

    def init_ui(self):
        main_layout = QVBoxLayout()
        container = QWidget()
        container.setLayout(main_layout)
        self.setCentralWidget(container)

        # 1. Topo
        top_panel = QGroupBox("Arquivo e Preview")
        top_layout = QHBoxLayout()
        
        btn_load = QPushButton("Carregar WAV...")
        btn_load.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_DialogOpenButton))
        btn_load.clicked.connect(self.load_file)
        
        self.lbl_info = QLabel("Nenhum arquivo carregado.")
        self.lbl_info.setStyleSheet("font-weight: bold; color: #ccc;")
        
        btn_play = QPushButton("Play Preview")
        btn_play.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_MediaPlay))
        btn_play.clicked.connect(self.play_audio)
        
        btn_stop = QPushButton("Stop")
        btn_stop.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_MediaStop))
        btn_stop.clicked.connect(self.stop_audio)
        
        top_layout.addWidget(btn_load)
        top_layout.addWidget(self.lbl_info, 1) 
        top_layout.addWidget(btn_play)
        top_layout.addWidget(btn_stop)
        top_panel.setLayout(top_layout)
        main_layout.addWidget(top_panel)

        # 2. Controles de Loop
        loop_panel = QGroupBox("Configuração de Loop")
        loop_layout = QHBoxLayout()
        
        loop_layout.addWidget(QLabel("Modo de Loop:"))
        self.combo_mode = QComboBox()
        self.combo_mode.addItems(["LOOP_OFF (None)", "LOOP_FORWARD (->)", "LOOP_PINGPONG (<->)", "LOOP_REVERSE (<-)"])
        self.combo_mode.currentIndexChanged.connect(self.on_loop_mode_changed)
        loop_layout.addWidget(self.combo_mode)
        
        loop_layout.addSpacing(20)
        
        loop_layout.addWidget(QLabel("Início:"))
        self.spin_start = QSpinBox()
        self.spin_start.setRange(0, 99999999)
        self.spin_start.setEnabled(False)
        self.spin_start.valueChanged.connect(self.on_spinbox_changed)
        loop_layout.addWidget(self.spin_start)
        
        loop_layout.addWidget(QLabel("Fim:"))
        self.spin_end = QSpinBox()
        self.spin_end.setRange(0, 99999999)
        self.spin_end.setEnabled(False)
        self.spin_end.valueChanged.connect(self.on_spinbox_changed)
        loop_layout.addWidget(self.spin_end)
        
        loop_panel.setLayout(loop_layout)
        main_layout.addWidget(loop_panel)

        # 3. Visualizador
        self.waveform = WaveformWidget()
        self.waveform.loopPointsChanged.connect(self.update_spinboxes_from_waveform)
        main_layout.addWidget(self.waveform, 1) 

        # 4. Botão Converter
        self.btn_convert = QPushButton("CONVERTER / GERAR CÓDIGO")
        self.btn_convert.setMinimumHeight(40)
        self.btn_convert.setStyleSheet("font-weight: bold; font-size: 14px; background-color: #007acc;")
        self.btn_convert.clicked.connect(self.generate_code)
        main_layout.addWidget(self.btn_convert)

        # 5. Áreas de Texto
        splitter = QSplitter(Qt.Orientation.Vertical)
        
        group_data = QGroupBox("1. Array de Dados C++")
        layout_data = QVBoxLayout()
        self.txt_data = QPlainTextEdit()
        self.txt_data.setReadOnly(True)
        self.txt_data.setFont(QFont("Consolas", 10)) 
        layout_data.addWidget(self.txt_data)
        group_data.setLayout(layout_data)
        
        group_instrument = QGroupBox("2. Configuração do Instrumento")
        layout_inst = QVBoxLayout()
        self.txt_instrument = QPlainTextEdit()
        self.txt_instrument.setReadOnly(True)
        self.txt_instrument.setFont(QFont("Consolas", 10))
        layout_inst.addWidget(self.txt_instrument)
        group_instrument.setLayout(layout_inst)
        
        splitter.addWidget(group_data)
        splitter.addWidget(group_instrument)
        splitter.setSizes([400, 200])
        
        main_layout.addWidget(splitter, 2) 

    def load_file(self):
        file_name, _ = QFileDialog.getOpenFileName(self, "Abrir Arquivo de Áudio", "", "WAV Files (*.wav);;All Files (*)")
        if file_name:
            try:
                self.filepath = file_name
                self.filename = os.path.splitext(os.path.basename(file_name))[0]
                self.filename = "".join([c if c.isalnum() else "_" for c in self.filename])

                # Lê o arquivo
                data, sr = sf.read(file_name, always_2d=True)
                self.sr = sr
                
                # Converte para int16 (formato do ESP32Synth)
                self.raw_data_int16 = (data * 32767).astype(np.int16)
                # Mixdown para mono (para visualização e geração)
                data_mono_float = np.mean(data, axis=1)
                self.raw_data_int16_mono = np.mean(self.raw_data_int16, axis=1).astype(np.int16)
                
                frames = len(data)
                duration = frames / sr
                self.lbl_info.setText(f"Arquivo: {os.path.basename(file_name)} | Taxa: {sr}Hz | Samples: {frames} | Duração: {duration:.2f}s")
                
                self.waveform.setData(data_mono_float)
                self.spin_start.setMaximum(frames - 1)
                self.spin_end.setMaximum(frames)
                self.spin_start.setValue(0)
                self.spin_end.setValue(frames) 
                
                self.combo_mode.setCurrentIndex(0) 
                self.on_loop_mode_changed(0)
                
                # Re-inicializa o mixer com a taxa de amostragem do arquivo
                # Isso garante que a reprodução não fique lenta/rápida em arquivos não-44k
                try:
                    pygame.mixer.quit()
                    pygame.mixer.init(frequency=self.sr, size=-16, channels=1, buffer=512)
                except Exception as e:
                    print(f"Erro ao reiniciar mixer: {e}")
                
                self.generate_code()

            except Exception as e:
                self.lbl_info.setText(f"Erro ao carregar: {e}")

    def on_loop_mode_changed(self, index):
        is_looping = index > 0
        self.spin_start.setEnabled(is_looping)
        self.spin_end.setEnabled(is_looping)
        self.waveform.setLoopEnabled(is_looping)
        if not is_looping:
            self.spin_start.setValue(0)
            self.spin_end.setValue(len(self.raw_data_int16_mono))

    def on_spinbox_changed(self):
        start = self.spin_start.value()
        end = self.spin_end.value()
        if start >= end:
             self.spin_start.setValue(end - 1)
        self.waveform.setLoopPoints(self.spin_start.value(), self.spin_end.value())

    def update_spinboxes_from_waveform(self, start, end):
        self.spin_start.blockSignals(True)
        self.spin_end.blockSignals(True)
        self.spin_start.setValue(start)
        self.spin_end.setValue(end)
        self.spin_start.blockSignals(False)
        self.spin_end.blockSignals(False)

    def play_audio(self):
        if self.raw_data_int16_mono is None: return
        self.stop_audio()

        loop_mode_idx = self.combo_mode.currentIndex()
        start = self.spin_start.value()
        end = self.spin_end.value()
        
        # --- LÓGICA DE PLAYBACK: ATAQUE -> LOOP ---
        # Simula o comportamento do Synth: toca o começo até o loop start,
        # depois fica preso no loop (ou toca tudo se não tiver loop).
        
        if loop_mode_idx > 0: # Tem Loop
            # Cria um buffer que contém o ATAQUE + VÁRIAS REPETIÇÕES DO LOOP
            # (Pygame não tem "loop point" nativo perfeito, então construímos o áudio)
            attack_part = self.raw_data_int16_mono[0:start]
            loop_part = self.raw_data_int16_mono[start:end]
            
            if len(loop_part) == 0: return

            # Repete o loop para durar pelo menos uns 10 segundos de preview
            required_samples = self.sr * 15 # 15 segundos
            repeats = int(required_samples / len(loop_part)) + 1
            if repeats > 100: repeats = 100 # Limite de segurança
            
            full_loop_sequence = np.tile(loop_part, repeats)
            preview_data = np.concatenate((attack_part, full_loop_sequence))
        else:
            # Sem Loop: Toca o arquivo inteiro
            preview_data = self.raw_data_int16_mono

        if len(preview_data) == 0: return

        # Fix Mono vs Stereo no mixer
        mixer_channels = pygame.mixer.get_init()[2]
        if mixer_channels == 2 and preview_data.ndim == 1:
            preview_data = np.repeat(preview_data[:, np.newaxis], 2, axis=1)

        sound_array = np.ascontiguousarray(preview_data)
        try:
            self.current_sound = pygame.sndarray.make_sound(sound_array)
            self.current_sound.play()
            
            # Inicia o Timer para atualizar o cursor visual
            self.playback_start_time = time.time()
            self.is_playing = True
            self.timer.start()
            
        except Exception as e:
            print(f"Erro no playback: {e}")
            self.lbl_info.setText(f"Erro de Playback: {e}")

    def update_playback_cursor(self):
        if not self.is_playing or not self.raw_data_int16_mono is not None:
            return

        elapsed_time = time.time() - self.playback_start_time
        # Calcula quantos samples já tocaram no total (linear)
        played_samples = int(elapsed_time * self.sr)
        
        loop_mode_idx = self.combo_mode.currentIndex()
        
        if loop_mode_idx > 0:
            # Lógica complexa para mapear o tempo linear de volta para a posição na onda original
            loop_start = self.spin_start.value()
            loop_end = self.spin_end.value()
            loop_len = loop_end - loop_start
            
            if loop_len <= 0: return

            if played_samples < loop_start:
                # Ainda está no ataque (antes do loop)
                visual_pos = played_samples
            else:
                # Já entrou no loop
                offset_in_loop = (played_samples - loop_start) % loop_len
                visual_pos = loop_start + offset_in_loop
        else:
            # Modo linear simples
            visual_pos = played_samples

        # Se acabou o som visualmente (passou do total), para o cursor
        if visual_pos >= len(self.raw_data_int16_mono) and loop_mode_idx == 0:
             self.waveform.setCursorPos(-1)
             self.timer.stop()
             self.is_playing = False
        else:
             self.waveform.setCursorPos(visual_pos)

    def stop_audio(self):
        if self.current_sound:
            self.current_sound.stop()
            self.current_sound = None
        self.timer.stop()
        self.is_playing = False
        self.waveform.setCursorPos(-1)

    def generate_code(self):
        if self.raw_data_int16_mono is None: return

        var_name = self.filename
        data_str = f"// Arquivo: {os.path.basename(self.filepath)}\n"
        data_str += f"// Taxa: {self.sr} Hz | Samples: {len(self.raw_data_int16_mono)}\n"
        data_str += f"const int16_t {var_name}_data[] = {{\n"
        
        line = "    "
        for i, sample in enumerate(self.raw_data_int16_mono):
            line += f"{sample}, "
            if (i + 1) % 16 == 0:
                data_str += line + "\n"
                line = "    "
        data_str += line + "\n};\n"
        data_str += f"const uint32_t {var_name}_len = {len(self.raw_data_int16_mono)};\n"
        data_str += f"const uint32_t {var_name}_rate = {self.sr};\n"
        
        self.txt_data.setPlainText(data_str)

        loop_mode_enum = ["LOOP_OFF", "LOOP_FORWARD", "LOOP_PINGPONG", "LOOP_REVERSE"][self.combo_mode.currentIndex()]
        loop_start_val = self.spin_start.value()
        loop_end_val = self.spin_end.value()
        if loop_end_val >= len(self.raw_data_int16_mono):
            loop_end_val = 0
            
        inst_str = f"// --- Configuração para setup() ---\n"
        inst_str += f"const SampleZone zonas_{var_name}[] = {{\n"
        inst_str += f"    {{ c0, g10, 0, c4 }} \n"
        inst_str += "};\n\n"

        inst_str += f"Instrument_Sample inst_{var_name} = {{\n"
        inst_str += f"    zonas_{var_name}, 1, {loop_mode_enum}, {loop_start_val}, {loop_end_val}\n"
        inst_str += "};\n\n"
        inst_str += f"// synth.registerSample(0, {var_name}_data, {var_name}_len, {var_name}_rate, c4);\n"
        inst_str += f"// synth.setInstrument(0, &inst_{var_name});\n"

        self.txt_instrument.setPlainText(inst_str)

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
        palette.setColor(QPalette.ColorRole.HighlightedText, Qt.GlobalColor.black)
        QApplication.setPalette(palette)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion") 
    window = MainWindow()
    window.show()
    sys.exit(app.exec())