import tkinter as tk
from tkinter import ttk
import serial
import serial.tools.list_ports
import mido
import time

class ESP32SynthController:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32Synth MIDI Controller - Pro Edition")
        self.root.geometry("600x560")
        
        self.serial_port = None
        self.midi_port = None
        
        # Flags para o sistema de Auto-Reconexão
        self.should_be_connected = False
        self.target_serial_name = ""
        self.target_midi_name = ""
        
        self.create_widgets()
        self.update_ports()
        
        # Inicia o Watchdog (Monitor de conexão em background)
        self.root.after(1000, self.watchdog)
        
    def create_widgets(self):
        # --- Configurações de Conexão ---
        conn_frame = ttk.LabelFrame(self.root, text="Connections & Status")
        conn_frame.pack(fill="x", padx=10, pady=5)
        
        ttk.Label(conn_frame, text="MIDI In:").grid(row=0, column=0, padx=5, pady=5, sticky="e")
        self.cb_midi = ttk.Combobox(conn_frame, width=30)
        self.cb_midi.grid(row=0, column=1, padx=5, pady=5)
        
        ttk.Label(conn_frame, text="Serial (ESP32):").grid(row=1, column=0, padx=5, pady=5, sticky="e")
        self.cb_serial = ttk.Combobox(conn_frame, width=30)
        self.cb_serial.grid(row=1, column=1, padx=5, pady=5)
        
        self.btn_connect = ttk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.btn_connect.grid(row=0, column=2, rowspan=2, padx=10, pady=5, sticky="nsew")
        
        # Status Visual
        self.status_label = tk.Label(conn_frame, text="Disconnected", fg="red", font=("Arial", 10, "bold"))
        self.status_label.grid(row=2, column=0, columnspan=3, pady=5)
        
        # --- Painel do Sintetizador ---
        synth_frame = ttk.Frame(self.root)
        synth_frame.pack(fill="both", expand=True, padx=10, pady=5)
        
        # Waveform
        ttk.Label(synth_frame, text="Waveform:").grid(row=0, column=0, sticky="w")
        self.wave_var = tk.StringVar(value="-1")
        wave_cb = ttk.Combobox(synth_frame, textvariable=self.wave_var, state="readonly", 
                               values=["-1 (Sine)", "-2 (Triangle)", "-3 (Saw)", "-4 (Pulse)", "-5 (Noise)"])
        wave_cb.grid(row=0, column=1, sticky="w", pady=5)
        wave_cb.bind("<<ComboboxSelected>>", lambda e: self.send_wave())
        
        # Master Volume
        self.master_vol = tk.IntVar(value=200)
        self.create_slider(synth_frame, "Master Volume", 0, 255, self.master_vol, 1, 0, self.send_master_vol)
        
        # Glide / Portamento
        self.glide_val = tk.IntVar(value=0)
        self.create_slider(synth_frame, "Glide / Portamento (ms)", 0, 1000, self.glide_val, 2, 0, self.send_glide)

        # ADSR Envelope
        env_frame = ttk.LabelFrame(synth_frame, text="ADSR Envelope")
        env_frame.grid(row=3, column=0, columnspan=2, sticky="ew", pady=10)
        self.a_val = tk.IntVar(value=10)
        self.d_val = tk.IntVar(value=100)
        self.s_val = tk.IntVar(value=200)
        self.r_val = tk.IntVar(value=300)
        self.create_slider(env_frame, "Attack (ms)", 0, 2000, self.a_val, 0, 0, self.send_env)
        self.create_slider(env_frame, "Decay (ms)", 0, 2000, self.d_val, 1, 0, self.send_env)
        self.create_slider(env_frame, "Sustain (0-255)", 0, 255, self.s_val, 2, 0, self.send_env)
        self.create_slider(env_frame, "Release (ms)", 0, 5000, self.r_val, 3, 0, self.send_env)
        
        # LFOs (Vibrato e Tremolo)
        lfo_frame = ttk.LabelFrame(synth_frame, text="LFOs (Vibrato & Tremolo)")
        lfo_frame.grid(row=4, column=0, columnspan=2, sticky="ew", pady=10)
        self.vib_r_val = tk.IntVar(value=0)
        self.vib_d_val = tk.IntVar(value=0)
        self.trm_r_val = tk.IntVar(value=0)
        self.trm_d_val = tk.IntVar(value=0)
        self.create_slider(lfo_frame, "Vibrato Rate", 0, 2000, self.vib_r_val, 0, 0, self.send_vibrato)
        self.create_slider(lfo_frame, "Vibrato Depth", 0, 1000, self.vib_d_val, 1, 0, self.send_vibrato)
        self.create_slider(lfo_frame, "Tremolo Rate", 0, 2000, self.trm_r_val, 2, 0, self.send_tremolo)
        self.create_slider(lfo_frame, "Tremolo Depth", 0, 255, self.trm_d_val, 3, 0, self.send_tremolo)

    def create_slider(self, parent, label, vmin, vmax, var, row, col, command):
        ttk.Label(parent, text=label).grid(row=row, column=col, sticky="w", padx=5)
        slider = ttk.Scale(parent, from_=vmin, to=vmax, variable=var, orient="horizontal", command=lambda e: command())
        slider.grid(row=row, column=col+1, sticky="ew", padx=5)
        parent.columnconfigure(col+1, weight=1)

    def update_ports(self):
        # Atualiza apenas se não estivermos forçando uma conexão
        if not self.should_be_connected:
            midi_ports = mido.get_input_names()
            self.cb_midi['values'] = midi_ports
            if midi_ports and self.cb_midi.get() not in midi_ports:
                self.cb_midi.current(0)
                
            serial_ports =[port.device for port in serial.tools.list_ports.comports()]
            self.cb_serial['values'] = serial_ports
            if serial_ports and self.cb_serial.get() not in serial_ports:
                self.cb_serial.current(0)

    def set_status(self, text, color):
        self.status_label.config(text=text, fg=color)

    def toggle_connection(self):
        if not self.should_be_connected:
            self.target_serial_name = self.cb_serial.get()
            self.target_midi_name = self.cb_midi.get()
            self.should_be_connected = True
            self.try_connect()
        else:
            self.should_be_connected = False
            self.disconnect_all()

    def try_connect(self):
        try:
            # Reconecta Serial a 921600 Baud
            if self.serial_port is None or not self.serial_port.is_open:
                self.serial_port = serial.Serial(self.target_serial_name, 921600, write_timeout=0)
            
            # Reconecta MIDI
            if self.midi_port is None:
                self.midi_port = mido.open_input(self.target_midi_name, callback=self.midi_callback)
            
            self.btn_connect.config(text="Disconnect")
            self.cb_midi.config(state="disabled")
            self.cb_serial.config(state="disabled")
            self.set_status("CONNECTED - Active", "green")
            
            # Sincroniza parâmetros
            self.send_wave()
            self.send_master_vol()
            self.send_env()
            self.send_glide()
            self.send_vibrato()
            self.send_tremolo()
            
        except Exception as e:
            self.disconnect_all(internal=True)
            self.set_status(f"Waiting for devices...", "orange")

    def disconnect_all(self, internal=False):
        if self.midi_port:
            try: self.midi_port.close()
            except: pass
        if self.serial_port:
            try: self.serial_port.close()
            except: pass
            
        self.serial_port = None
        self.midi_port = None
        
        if not internal:
            self.btn_connect.config(text="Connect")
            self.cb_midi.config(state="readonly")
            self.cb_serial.config(state="readonly")
            self.set_status("Disconnected", "red")
            self.update_ports()

    def watchdog(self):
        # Sistema que vigia as conexões a cada 1 segundo
        if self.should_be_connected:
            # Pega lista de hardware plugado no PC agora
            current_serial_ports = [port.device for port in serial.tools.list_ports.comports()]
            current_midi_ports = mido.get_input_names()
            
            serial_lost = self.target_serial_name not in current_serial_ports
            midi_lost = self.target_midi_name not in current_midi_ports
            
            # Checa se deu erro na escrita serial
            port_broken = (self.serial_port is None) or (not self.serial_port.is_open)
            
            if serial_lost or midi_lost or port_broken:
                self.set_status("Connection Lost! Auto-reconnecting...", "orange")
                self.disconnect_all(internal=True)
                self.try_connect() # Tenta puxar de volta
                
        else:
            self.update_ports() # Se desligado, fica atualizando as listas
            
        # Reagenda o watchdog
        self.root.after(1000, self.watchdog)

    # --- Comandos Serial ---
    def serial_send(self, data_str):
        if self.should_be_connected and self.serial_port and self.serial_port.is_open:
            try:
                self.serial_port.write((data_str + '\n').encode('ascii'))
            except Exception:
                # Se falhar ao escrever (ex: puxou o cabo tocando), o watchdog pega no próximo segundo
                self.serial_port.close()
                self.serial_port = None

    def send_wave(self):
        wave_code = self.wave_var.get().split()[0]
        self.serial_send(f"W {wave_code}")

    def send_master_vol(self):
        self.serial_send(f"M {self.master_vol.get()}")

    def send_env(self):
        self.serial_send(f"E {self.a_val.get()} {self.d_val.get()} {self.s_val.get()} {self.r_val.get()}")

    def send_glide(self):
        self.serial_send(f"P {self.glide_val.get()}")

    def send_vibrato(self):
        self.serial_send(f"V {self.vib_r_val.get()} {self.vib_d_val.get()}")

    def send_tremolo(self):
        self.serial_send(f"T {self.trm_r_val.get()} {self.trm_d_val.get()}")

    # --- Callback MIDI ---
    def midi_callback(self, msg):
        try:
            if msg.type == 'note_on':
                if msg.velocity > 0:
                    self.serial_send(f"N {msg.note} {msg.velocity}")
                else:
                    self.serial_send(f"F {msg.note}")
            elif msg.type == 'note_off':
                self.serial_send(f"F {msg.note}")
        except:
            pass 

if __name__ == "__main__":
    root = tk.Tk()
    app = ESP32SynthController(root)
    root.mainloop()