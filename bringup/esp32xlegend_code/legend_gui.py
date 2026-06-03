import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import time

REGISTERS = {
    "0x0: Default State (R)": 0x0,
    "0x1: Debug State (R)": 0x1,
    "0x2: Output Buffer Ctrl (R/W)": 0x2,
    "0x3: Latch OHT [0:15] (R)": 0x3,
    "0x4: Latch OHT [16:31] (R)": 0x4,
    "0x5: Jitter OHT [0:15] (R)": 0x5,
    "0x6: Jitter OHT [16:31] (R)": 0x6,
    "0x7: Latch Calibration (R/W)": 0x7,
    "0x8: Temp Thresh 0 (R/W)": 0x8,
    "0x9: Temp Thresh 1 (R/W)": 0x9,
    "0xA: Temp Thresh 2 (R/W)": 0xA,
    "0xB: Temp Thresh 3 (R/W)": 0xB,
    "0xC: Temp Counter 0 (R)": 0xC,
    "0xD: Temp Counter 1 (R)": 0xD,
    "0xE: Temp Counter 2 (R)": 0xE,
    "0xF: Temp Counter 3 (R)": 0xF,
}

MOD_SELECTS = {
    "Fixed/Status": 0,
    "Latch Entropy": 1,
    "Jitter Entropy": 2,
    "Unused": 3
}

IN_MOD_SELECTS = {
    "None": 0,
    "Latch OHT MUX In": 1,
    "Jitter OHT MUX In": 2,
    "CTD Debug In": 3
}

class LegendGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("LEGEND ASIC Debug Interface")
        self.serial_port = None
        
        # Apply dark theme styling
        self.root.configure(bg="#1e1e1e")
        style = ttk.Style(self.root)
        if "clam" in style.theme_names():
            style.theme_use("clam")
            
        style.configure(".", background="#1e1e1e", foreground="#ffffff", font=("Segoe UI", 10))
        style.configure("TLabelframe", background="#1e1e1e", foreground="#00ffcc", bordercolor="#333333", darkcolor="#333333", lightcolor="#333333")
        style.configure("TLabelframe.Label", background="#1e1e1e", foreground="#00ffcc", font=("Segoe UI", 10, "bold"))
        style.configure("TButton", background="#333333", foreground="#00ffcc", borderwidth=1, bordercolor="#555555", padding=5, font=("Segoe UI", 9, "bold"))
        style.map("TButton", background=[("active", "#444444")])
        style.configure("TCombobox", fieldbackground="#333333", background="#333333", foreground="#ffffff", arrowcolor="#00ffcc")
        style.map("TCombobox", fieldbackground=[("readonly", "#333333")])
        style.configure("TEntry", fieldbackground="#333333", foreground="#ffffff", insertcolor="#ffffff")
        style.configure("TSpinbox", fieldbackground="#333333", background="#333333", foreground="#ffffff", arrowcolor="#00ffcc")
        
        self.root.option_add("*TCombobox*Listbox.background", "#333333")
        self.root.option_add("*TCombobox*Listbox.foreground", "#ffffff")
        self.root.option_add("*TCombobox*Listbox.selectBackground", "#00ffcc")
        self.root.option_add("*TCombobox*Listbox.selectForeground", "#000000")
        
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)
        import signal
        signal.signal(signal.SIGINT, lambda sig, frame: self.on_closing())
        
        self.create_widgets()
        self.refresh_ports()

    def on_closing(self):
        if self.serial_port and self.serial_port.is_open:
            try:
                self.serial_port.close()
            except:
                pass
        self.root.destroy()
        import sys
        sys.exit(0)

    def create_widgets(self):
        # --- Connection Frame ---
        conn_frame = ttk.LabelFrame(self.root, text="Connection")
        conn_frame.grid(row=0, column=0, padx=10, pady=5, sticky="ew")
        
        self.port_combo = ttk.Combobox(conn_frame, state="readonly", width=15)
        self.port_combo.pack(side=tk.LEFT, padx=5, pady=5)
        
        ttk.Button(conn_frame, text="Refresh", command=self.refresh_ports).pack(side=tk.LEFT, padx=5)
        self.btn_connect = ttk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.btn_connect.pack(side=tk.LEFT, padx=5)
        
        # --- Mux Configuration Frame ---
        mux_frame = ttk.LabelFrame(self.root, text="MUX Configuration (Bit 21=0)")
        mux_frame.grid(row=1, column=0, padx=10, pady=5, sticky="ew")
        
        # Output 2
        ttk.Label(mux_frame, text="Output 2 Mod:").grid(row=0, column=0, sticky="e")
        self.out2_mod = ttk.Combobox(mux_frame, values=list(MOD_SELECTS.keys()), state="readonly", width=12)
        self.out2_mod.current(0)
        self.out2_mod.grid(row=0, column=1, padx=5, pady=2)
        
        ttk.Label(mux_frame, text="Out 2 Cell (0-31):").grid(row=0, column=2, sticky="e")
        self.out2_cell = ttk.Spinbox(mux_frame, from_=0, to=31, width=5)
        self.out2_cell.set(0)
        self.out2_cell.grid(row=0, column=3, padx=5, pady=2)

        # Output 1
        ttk.Label(mux_frame, text="Output 1 Mod:").grid(row=1, column=0, sticky="e")
        self.out1_mod = ttk.Combobox(mux_frame, values=list(MOD_SELECTS.keys()), state="readonly", width=12)
        self.out1_mod.current(0)
        self.out1_mod.grid(row=1, column=1, padx=5, pady=2)
        
        ttk.Label(mux_frame, text="Out 1 Cell (0-31):").grid(row=1, column=2, sticky="e")
        self.out1_cell = ttk.Spinbox(mux_frame, from_=0, to=31, width=5)
        self.out1_cell.set(0)
        self.out1_cell.grid(row=1, column=3, padx=5, pady=2)
        
        # Input Mod
        ttk.Label(mux_frame, text="Input Mod:").grid(row=2, column=0, sticky="e")
        self.in_mod = ttk.Combobox(mux_frame, values=list(IN_MOD_SELECTS.keys()), state="readonly", width=12)
        self.in_mod.current(0)
        self.in_mod.grid(row=2, column=1, padx=5, pady=2)
        
        ttk.Label(mux_frame, text="Input Cell (0-31):").grid(row=2, column=2, sticky="e")
        self.in_cell = ttk.Spinbox(mux_frame, from_=0, to=31, width=5)
        self.in_cell.set(0)
        self.in_cell.grid(row=2, column=3, padx=5, pady=2)
        
        ttk.Button(mux_frame, text="Apply MUX", command=self.apply_mux).grid(row=3, column=0, columnspan=4, pady=5)

        # --- Register Frame ---
        reg_frame = ttk.LabelFrame(self.root, text="Register Access (Bit 21=1)")
        reg_frame.grid(row=2, column=0, padx=10, pady=5, sticky="ew")
        
        self.reg_combo = ttk.Combobox(reg_frame, values=list(REGISTERS.keys()), state="readonly", width=30)
        self.reg_combo.current(7)
        self.reg_combo.grid(row=0, column=0, columnspan=2, padx=5, pady=5)
        
        ttk.Label(reg_frame, text="Value (Hex):").grid(row=1, column=0, sticky="e")
        self.reg_val = ttk.Entry(reg_frame, width=10)
        self.reg_val.insert(0, "0")
        self.reg_val.grid(row=1, column=1, sticky="w", padx=5)
        
        ttk.Button(reg_frame, text="Read", command=self.read_reg).grid(row=2, column=0, pady=5)
        ttk.Button(reg_frame, text="Write", command=self.write_reg).grid(row=2, column=1, pady=5)

        # --- Actions Frame ---
        act_frame = ttk.LabelFrame(self.root, text="Actions & Diagnostics")
        act_frame.grid(row=3, column=0, padx=10, pady=5, sticky="ew")
        
        ttk.Button(act_frame, text="Classify Output 1", command=lambda: self.send_cmd("C 1")).grid(row=0, column=0, padx=5, pady=5)
        ttk.Button(act_frame, text="Classify Output 2", command=lambda: self.send_cmd("C 2")).grid(row=0, column=1, padx=5, pady=5)
        ttk.Button(act_frame, text="Reset ASIC", command=lambda: self.send_cmd("X")).grid(row=0, column=2, padx=5, pady=5)

        ttk.Button(act_frame, text="Check OHT", command=self.check_oht).grid(row=1, column=0, padx=5, pady=5)
        ttk.Button(act_frame, text="Dump All Registers", command=self.dump_all_regs).grid(row=1, column=1, padx=5, pady=5)

        ttk.Button(act_frame, text="Mirror Out 1 to LED (5s)", command=lambda: self.send_cmd("M 1 5000")).grid(row=2, column=0, padx=5, pady=5)
        ttk.Button(act_frame, text="Mirror Out 2 to LED (5s)", command=lambda: self.send_cmd("M 2 5000")).grid(row=2, column=1, padx=5, pady=5)

        # Count Samples (E command) — cross-validate calibration sweep data
        cnt_frame = ttk.Frame(act_frame)
        cnt_frame.grid(row=3, column=0, columnspan=3, pady=5)
        ttk.Label(cnt_frame, text="Count Samples:").pack(side=tk.LEFT, padx=5)
        self.cnt_val = ttk.Entry(cnt_frame, width=8)
        self.cnt_val.insert(0, "1000")
        self.cnt_val.pack(side=tk.LEFT, padx=5)
        ttk.Button(cnt_frame, text="Count Ones", command=self.count_samples).pack(side=tk.LEFT, padx=5)
        self.cnt_result = ttk.Label(cnt_frame, text="Result: —", foreground="#2ca02c", font=("Courier", 10, "bold"))
        self.cnt_result.pack(side=tk.LEFT, padx=10)

        # Clock Control
        clk_frame = ttk.Frame(act_frame)
        clk_frame.grid(row=4, column=0, columnspan=3, pady=5)
        ttk.Label(clk_frame, text="Clock Half-Period (µs):").pack(side=tk.LEFT, padx=5)
        self.clk_val = ttk.Entry(clk_frame, width=8)
        self.clk_val.insert(0, "500")
        self.clk_val.pack(side=tk.LEFT, padx=5)
        ttk.Button(clk_frame, text="Set Freq", command=lambda: self.send_cmd(f"F {self.clk_val.get()}")).pack(side=tk.LEFT, padx=5)

        # --- Log Frame ---
        log_frame = ttk.LabelFrame(self.root, text="Log")
        log_frame.grid(row=4, column=0, padx=10, pady=5, sticky="nsew")
        
        self.log_text = scrolledtext.ScrolledText(log_frame, width=60, height=15, bg="black", fg="green", insertbackground="green")
        self.log_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

    def log(self, msg):
        self.log_text.insert(tk.END, msg + "\n")
        self.log_text.see(tk.END)

    def refresh_ports(self):
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        if ports:
            self.port_combo.current(0)

    def toggle_connection(self):
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            self.btn_connect.config(text="Connect")
            self.log("Disconnected.")
        else:
            port = self.port_combo.get()
            if not port: return
            try:
                self.serial_port = serial.Serial(port, 115200, timeout=5.0)
                self.btn_connect.config(text="Disconnect")
                self.log(f"Connected to {port} at 115200 baud.")
                # Give Arduino time to reset
                time.sleep(2)
                self.serial_port.reset_input_buffer()
            except Exception as e:
                messagebox.showerror("Connection Error", str(e))

    def send_cmd(self, cmd, wait_for_response=True):
        if not self.serial_port or not self.serial_port.is_open:
            self.log("Error: Not connected.")
            return None
        
        try:
            # Flush any stale data from previous timeouts to ensure we only read the response to THIS command
            self.serial_port.reset_input_buffer()
            
            self.log(f"-> {cmd}")
            self.serial_port.write((cmd + "\n").encode())
            
            if wait_for_response:
                # Poll with root.update() to keep the GUI responsive
                start_time = time.time()
                while getattr(self.serial_port, 'in_waiting', 0) == 0:
                    if time.time() - start_time > 6.0:
                        break
                    self.root.update()
                    time.sleep(0.01)

                resp = self.serial_port.readline().decode().strip()
                self.log(f"<- {resp}")
                return resp
            return None
        except Exception as e:
            self.log(f"Serial communication interrupted: {e}")
            return None

    def apply_mux(self):
        m2 = MOD_SELECTS[self.out2_mod.get()]
        c2 = int(self.out2_cell.get())
        m1 = MOD_SELECTS[self.out1_mod.get()]
        c1 = int(self.out1_cell.get())
        minp = IN_MOD_SELECTS[self.in_mod.get()]
        cinp = int(self.in_cell.get())
        
        cmd = (0 << 21) | (m2 << 19) | (c2 << 14) | (m1 << 12) | (c1 << 7) | (minp << 5) | (cinp << 0)
        self.send_cmd(f"W {cmd:X}")

    def read_reg(self):
        reg_name = self.reg_combo.get()
        reg_addr = REGISTERS[reg_name]
        resp = self.send_cmd(f"R {reg_addr:X}")
        if resp and resp.startswith("OK"):
            parts = resp.split(" ")
            if len(parts) > 1:
                val_hex = parts[1]
                self.reg_val.delete(0, tk.END)
                self.reg_val.insert(0, val_hex)
            else:
                self.log("Error: Register Read returned OK but no value.")

    def write_reg(self):
        reg_name = self.reg_combo.get()
        reg_addr = REGISTERS[reg_name]
        try:
            val = int(self.reg_val.get(), 16)
        except ValueError:
            self.log("Error: Invalid hex value.")
            return
            
        cmd = (1 << 21) | (0 << 20) | (reg_addr << 16) | (val & 0xFFFF)
        self.send_cmd(f"W {cmd:X}")

    def check_oht(self):
        def parse_oht(name, val):
            try:
                v = int(val, 16)
                good = [i for i in range(16) if (v & (1 << i))]
                self.log(f"{name} Good Sources: {good if good else 'None'}")
            except: pass

        resp = self.send_cmd("R 3") # Latch Lower
        if resp and resp.startswith("OK") and len(resp.split(" ")) > 1: parse_oht("Latch [0:15]", resp.split(" ")[1])
        
        resp = self.send_cmd("R 4") # Latch Upper
        if resp and resp.startswith("OK") and len(resp.split(" ")) > 1: parse_oht("Latch [16:31]", resp.split(" ")[1])
        
        resp = self.send_cmd("R 5") # Jitter Lower
        if resp and resp.startswith("OK") and len(resp.split(" ")) > 1: parse_oht("Jitter [0:15]", resp.split(" ")[1])
        
        resp = self.send_cmd("R 6") # Jitter Upper
        if resp and resp.startswith("OK") and len(resp.split(" ")) > 1: parse_oht("Jitter [16:31]", resp.split(" ")[1])

    def dump_all_regs(self):
        self.log("\n--- DUMPING ALL REGISTERS ---")
        for name, addr in REGISTERS.items():
            resp = self.send_cmd(f"R {addr:X}", wait_for_response=True)
            if resp and resp.startswith("OK") and len(resp.split(" ")) > 1:
                val = resp.split(" ")[1]
                # Format to remove the "0xX:" prefix from the name for cleaner printing
                clean_name = name.split(":", 1)[1].strip() if ":" in name else name
                self.log(f"Reg 0x{addr:X} ({clean_name}): 0x{val}")
            else:
                self.log(f"Reg 0x{addr:X} ({name}): Read Failed")
            time.sleep(0.05) # Small delay to not overwhelm the serial buffer
        self.log("--- END DUMP ---\n")

    def count_samples(self):
        n = self.cnt_val.get().strip()
        if not n.isdigit():
            self.log("Error: Sample count must be an integer.")
            return
        resp = self.send_cmd(f"E {n}")
        if resp and resp.startswith("OK") and len(resp.split(" ")) > 1:
            ones = int(resp.split(" ")[1])
            total = int(n)
            pct = ones / total * 100
            result_str = f"{ones}/{total}  ({pct:.2f}%)"
            self.cnt_result.config(text=f"Result: {result_str}")
            self.log(f"Count Ones: {result_str}")
        else:
            self.cnt_result.config(text="Result: Error")
            self.log(f"Count Ones failed: {resp}")

if __name__ == "__main__":
    root = tk.Tk()
    app = LegendGUI(root)
    root.mainloop()
