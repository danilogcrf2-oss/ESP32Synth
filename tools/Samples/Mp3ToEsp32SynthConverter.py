import sys
import os
import time
import pygame
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, 
    QPushButton, QLabel, QFileDialog, QPlainTextEdit, QGroupBox, 
    QStyle, QMessageBox, QProgressBar
)
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QPalette, QColor, QFont

TRANSLATIONS = {
    "pt": {
        "title": "Conversor MP3 Flash para ESP32Synth (v1.0)",
        "grp_file": "Arquivo e Preview",
        "btn_load": "Carregar MP3...",
        "no_file": "Nenhum arquivo carregado.",
        "btn_play": "Play Preview",
        "btn_stop": "Stop",
        "btn_export": "EXPORTAR ARQUIVO .H",
        "grp_code": "Código C++ Gerado (Preview)",
        "dlg_title": "Abrir Arquivo MP3",
        "dlg_export": "Salvar Arquivo .h",
        "file_info": "Arquivo: {0} | Tamanho: {1:.2f} KB | Flash: PROGMEM",
        "btn_lang": "🇺🇸 English",
        "export_success": "Arquivo salvo com sucesso em:\n{0}",
        "export_empty": "Nenhum arquivo carregado para exportar!",
        "export_saving": "Exportando arquivo, aguarde..."
    },
    "en": {
        "title": "MP3 Flash Converter for ESP32Synth (v1.0)",
        "grp_file": "File & Preview",
        "btn_load": "Load MP3...",
        "no_file": "No file loaded.",
        "btn_play": "Play Preview",
        "btn_stop": "Stop",
        "btn_export": "EXPORT .H FILE",
        "grp_code": "Generated C++ Code (Preview)",
        "dlg_title": "Open MP3 File",
        "dlg_export": "Save .h File",
        "file_info": "File: {0} | Size: {1:.2f} KB | Flash: PROGMEM",
        "btn_lang": "🇧🇷 Português",
        "export_success": "File successfully saved to:\n{0}",
        "export_empty": "No file loaded to export!",
        "export_saving": "Exporting file, please wait..."
    }
}

class MP3ConverterWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.cur_lang = "pt"
        self.setMinimumSize(850, 600)
        
        self.filepath = ""
        self.filename = "audio_track"
        self.raw_mp3_bytes = None
        
        pygame.mixer.init()

        self.init_ui()
        self.apply_dark_theme()
        self.update_texts()

    def init_ui(self):
        main_layout = QVBoxLayout()
        container = QWidget()
        container.setLayout(main_layout)
        self.setCentralWidget(container)

        # Top Bar: Idioma
        top_bar = QHBoxLayout()
        top_bar.addStretch()
        self.btn_lang = QPushButton("🇺🇸 English")
        self.btn_lang.setFixedWidth(120)
        self.btn_lang.clicked.connect(self.toggle_language)
        top_bar.addWidget(self.btn_lang)
        main_layout.addLayout(top_bar)

        # Grupo: Arquivo e Preview
        self.group_file = QGroupBox("Arquivo e Preview")
        file_layout = QHBoxLayout()
        
        self.btn_load = QPushButton("Carregar MP3...")
        self.btn_load.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_DialogOpenButton))
        self.btn_load.clicked.connect(self.load_file)
        
        self.lbl_info = QLabel("Nenhum arquivo carregado.")
        self.lbl_info.setStyleSheet("font-weight: bold; color: #ccc;")
        
        self.btn_play = QPushButton("Play Preview")
        self.btn_play.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_MediaPlay))
        self.btn_play.clicked.connect(self.play_audio)
        
        self.btn_stop = QPushButton("Stop")
        self.btn_stop.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_MediaStop))
        self.btn_stop.clicked.connect(self.stop_audio)
        
        file_layout.addWidget(self.btn_load)
        file_layout.addWidget(self.lbl_info, 1)
        file_layout.addWidget(self.btn_play)
        file_layout.addWidget(self.btn_stop)
        self.group_file.setLayout(file_layout)
        main_layout.addWidget(self.group_file)

        # Grupo: Preview do Código
        self.group_code = QGroupBox("Código C++ Gerado (Preview)")
        code_layout = QVBoxLayout()
        self.txt_preview = QPlainTextEdit()
        self.txt_preview.setReadOnly(True)
        self.txt_preview.setFont(QFont("Consolas", 10))
        code_layout.addWidget(self.txt_preview)
        self.group_code.setLayout(code_layout)
        main_layout.addWidget(self.group_code, 1)

        # Botão de Exportação
        self.btn_export = QPushButton("EXPORTAR ARQUIVO .H")
        self.btn_export.setMinimumHeight(45)
        self.btn_export.setStyleSheet("font-weight: bold; font-size: 14px; background-color: #2e8b57; color: white;")
        self.btn_export.clicked.connect(self.export_h_file)
        main_layout.addWidget(self.btn_export)

    def toggle_language(self):
        self.cur_lang = "en" if self.cur_lang == "pt" else "pt"
        self.update_texts()

    def update_texts(self):
        t = TRANSLATIONS[self.cur_lang]
        self.setWindowTitle(t["title"])
        self.btn_lang.setText(t["btn_lang"])
        self.group_file.setTitle(t["grp_file"])
        self.btn_load.setText(t["btn_load"])
        self.btn_play.setText(t["btn_play"])
        self.btn_stop.setText(t["btn_stop"])
        self.group_code.setTitle(t["grp_code"])
        self.btn_export.setText(t["btn_export"])
        
        if self.raw_mp3_bytes is None:
            self.lbl_info.setText(t["no_file"])
        else:
            size_kb = len(self.raw_mp3_bytes) / 1024.0
            self.lbl_info.setText(t["file_info"].format(os.path.basename(self.filepath), size_kb))

    def load_file(self):
        t = TRANSLATIONS[self.cur_lang]
        file_name, _ = QFileDialog.getOpenFileName(self, t["dlg_title"], "", "MP3 Files (*.mp3);;All Files (*)")
        if file_name:
            try:
                self.filepath = file_name
                self.filename = os.path.splitext(os.path.basename(file_name))[0]
                self.filename = "".join([c if c.isalnum() else "_" for c in self.filename])

                with open(file_name, "rb") as f:
                    self.raw_mp3_bytes = f.read()

                size_kb = len(self.raw_mp3_bytes) / 1024.0
                self.lbl_info.setText(t["file_info"].format(os.path.basename(file_name), size_kb))
                
                self.update_preview()
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Could not load MP3: {e}")

    def update_preview(self):
        if not self.raw_mp3_bytes: return
        
        var_name = self.filename
        size = len(self.raw_mp3_bytes)
        
        # Gera apenas os primeiros 128 bytes para o preview da interface não engasgar
        preview_bytes = self.raw_mp3_bytes[:128]
        hex_lines = []
        for i in range(0, len(preview_bytes), 16):
            chunk = preview_bytes[i:i+16]
            hex_lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")

        preview_str = f"// File: {os.path.basename(self.filepath)}\n"
        preview_str += f"// Size: {size} bytes ({size/1024:.2f} KB)\n\n"
        preview_str += f"const uint8_t {var_name}_mp3[] PROGMEM = {{\n"
        preview_str += "\n".join(hex_lines) + "\n"
        preview_str += f"    // ... [{size - 128} bytes truncated in UI preview] ...\n"
        preview_str += "};\n\n"
        preview_str += f"const uint32_t {var_name}_mp3_len = {size};\n\n"
        preview_str += f"// --- Playback usage ---\n"
        preview_str += f"// synth.playMP3StreamFromFlash(0, {var_name}_mp3, {var_name}_mp3_len, 200, 4800000, true);\n"

        self.txt_preview.setPlainText(preview_str)

    def play_audio(self):
        if not self.filepath: return
        try:
            pygame.mixer.music.load(self.filepath)
            pygame.mixer.music.play()
        except Exception as e:
            print(f"Error previewing MP3: {e}")

    def stop_audio(self):
        pygame.mixer.music.stop()

    def export_h_file(self):
        t = TRANSLATIONS[self.cur_lang]
        if not self.raw_mp3_bytes:
            QMessageBox.warning(self, "Aviso", t["export_empty"])
            return

        file_path, _ = QFileDialog.getSaveFileName(self, t["dlg_export"], f"{self.filename}.h", "C/C++ Header Files (*.h)")
        if not file_path: return

        var_name = self.filename
        name_upper = self.filename.upper()
        total_bytes = len(self.raw_mp3_bytes)

        try:
            # Escrita direta em buffer para velocidade máxima e zero estouro de memória
            with open(file_path, "w", encoding="utf-8") as f:
                f.write(f"#ifndef {name_upper}_H\n")
                f.write(f"#define {name_upper}_H\n\n")
                f.write("#include <Arduino.h>\n\n")
                f.write(f"// File: {os.path.basename(self.filepath)}\n")
                f.write(f"// Size: {total_bytes} bytes\n")
                f.write(f"const uint8_t {var_name}_mp3[] PROGMEM = {{\n")

                # Escreve em blocos de 16 bytes formatados
                chunk_size = 16
                for i in range(0, total_bytes, chunk_size):
                    chunk = self.raw_mp3_bytes[i:i+chunk_size]
                    line = "    " + ", ".join(f"0x{b:02X}" for b in chunk)
                    if i + chunk_size < total_bytes:
                        line += ",\n"
                    else:
                        line += "\n"
                    f.write(line)

                f.write("};\n\n")
                f.write(f"const uint32_t {var_name}_mp3_len = {total_bytes};\n\n")
                f.write(f"#endif // {name_upper}_H\n")

            QMessageBox.information(self, "Sucesso", t["export_success"].format(file_path))
        except Exception as e:
            QMessageBox.critical(self, "Erro", f"Export failed:\n{e}")

    def apply_dark_theme(self):
        palette = QPalette()
        palette.setColor(QPalette.ColorRole.Window, QColor(53, 53, 53))
        palette.setColor(QPalette.ColorRole.WindowText, Qt.GlobalColor.white)
        palette.setColor(QPalette.ColorRole.Base, QColor(25, 25, 25))
        palette.setColor(QPalette.ColorRole.AlternateBase, QColor(53, 53, 53))
        palette.setColor(QPalette.ColorRole.Text, Qt.GlobalColor.white)
        palette.setColor(QPalette.ColorRole.Button, QColor(53, 53, 53))
        palette.setColor(QPalette.ColorRole.ButtonText, Qt.GlobalColor.white)
        palette.setColor(QPalette.ColorRole.Highlight, QColor(42, 130, 218))
        palette.setColor(QPalette.ColorRole.HighlightedText, Qt.GlobalColor.black)
        QApplication.setPalette(palette)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    win = MP3ConverterWindow()
    win.show()
    sys.exit(app.exec())