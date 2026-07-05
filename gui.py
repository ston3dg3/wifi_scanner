"""
WiFi Analyzer GUI - reads the compact serial protocol emitted by the
ESP8266 firmware (see wifi_analyzer/include/protocol.h) and renders it as
a live dashboard: nearby networks plotted by channel/signal strength,
devices joining them, and deauth/disassoc/reauth alerts.

Serial protocol (one line each, pipe-delimited):
    BEGIN
    NET|channel|ssid|rssi|secured(0/1)|bssid
    ASSOC|deviceMac|ssid|bssid|lastSeenSecondsAgo
    ALERT|kind|mac1|mac2|ageSecondsAgo
    END|uptimeSeconds|totalAlertCount
Any other line is treated as plain log text.
"""

import queue
import threading
import tkinter as tk
from tkinter import ttk

import customtkinter as ctk
import serial
import serial.tools.list_ports

BAUD_RATE = 9600
POLL_INTERVAL_MS = 150

# --- Theme -----------------------------------------------------------------
COLOR_BG = "#0a0e14"
COLOR_PANEL_BG = "#111826"
COLOR_PANEL_BORDER = "#2a3444"
COLOR_GRID = "#1b2331"
COLOR_TEXT = "#d8dee9"
COLOR_TEXT_DIM = "#6b7688"
COLOR_YELLOW = "#e8c547"
COLOR_BLUE = "#4fa3e0"
COLOR_GREEN = "#4fd68c"
COLOR_RED = "#e0555f"
COLOR_PINK = "#d199e0"

FONT_MONO = ("Consolas", 11)
FONT_MONO_BOLD = ("Consolas", 11, "bold")
FONT_MONO_SMALL = ("Consolas", 9)
FONT_MONO_SMALL_BOLD = ("Consolas", 9, "bold")

RSSI_MAX = -30  # top of the chart (strong signal)
RSSI_MIN = -100  # bottom of the chart (weak signal)


class SerialReader(threading.Thread):
    """Reads lines from a serial port on a background thread and pushes
    them into a queue for the GUI thread to consume."""

    def __init__(self, port: str, baud: int, line_queue: "queue.Queue[str]", stop_event: threading.Event):
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.line_queue = line_queue
        self.stop_event = stop_event

    def run(self):
        try:
            with serial.Serial(self.port, self.baud, timeout=1) as ser:
                while not self.stop_event.is_set():
                    raw = ser.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="replace").strip()
                    if line:
                        self.line_queue.put(line)
        except serial.SerialException as exc:
            self.line_queue.put(f"__ERROR__|{exc}")


class WifiAnalyzerApp(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("WiFi Analyzer Dashboard")
        self.geometry("1200x720")
        self.configure(fg_color=COLOR_BG)
        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("blue")

        self.line_queue: "queue.Queue[str]" = queue.Queue()
        self.stop_event = threading.Event()
        self.reader_thread = None  # type: SerialReader | None

        self._pending_networks = []
        self._pending_associations = []
        self._pending_alerts = []
        self._current_networks = []  # parsed dicts, kept for canvas redraws on resize

        self._build_ui()
        self.after(POLL_INTERVAL_MS, self._poll_serial_queue)

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self):
        top = ctk.CTkFrame(self, fg_color=COLOR_PANEL_BG, border_width=1,
                            border_color=COLOR_PANEL_BORDER, corner_radius=6)
        top.pack(fill="x", padx=10, pady=(10, 6))

        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_var = tk.StringVar(value=ports[0] if ports else "No ports found")
        self.port_menu = ctk.CTkComboBox(top, values=ports or ["No ports found"],
                                          variable=self.port_var, width=200,
                                          font=FONT_MONO, dropdown_font=FONT_MONO)
        self.port_menu.pack(side="left", padx=10, pady=10)

        ctk.CTkButton(top, text="Refresh Ports", width=120, font=FONT_MONO,
                       fg_color=COLOR_PANEL_BORDER, hover_color="#3a4457",
                       command=self._refresh_ports).pack(side="left", padx=(0, 10), pady=10)

        self.connect_btn = ctk.CTkButton(top, text="Connect", width=120, font=FONT_MONO_BOLD,
                                          fg_color=COLOR_GREEN, text_color="#0a0e14",
                                          hover_color="#3fb877",
                                          command=self._toggle_connection)
        self.connect_btn.pack(side="left", padx=(0, 10), pady=10)

        self.status_label = ctk.CTkLabel(top, text="○ Disconnected", text_color=COLOR_RED, font=FONT_MONO_BOLD)
        self.status_label.pack(side="left", padx=(10, 0))

        self.uptime_label = ctk.CTkLabel(top, text="", text_color=COLOR_TEXT_DIM, font=FONT_MONO)
        self.uptime_label.pack(side="right", padx=10)

        self.tabview = ctk.CTkTabview(self, fg_color=COLOR_PANEL_BG,
                                       segmented_button_selected_color=COLOR_BLUE,
                                       segmented_button_unselected_color=COLOR_PANEL_BG,
                                       segmented_button_fg_color=COLOR_PANEL_BORDER,
                                       text_color=COLOR_TEXT)
        self.tabview.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        self.tabview.add("Networks")
        self.tabview.add("Devices")
        self.tabview.add("Alerts")
        self.tabview.add("Log")
        for name in ("Networks", "Devices", "Alerts", "Log"):
            self.tabview.tab(name).configure(fg_color=COLOR_BG)

        self._style_treeview()
        self._build_networks_canvas()

        self.devices_tree = self._make_tree(
            self.tabview.tab("Devices"),
            columns=("mac", "ssid", "bssid", "last"),
            headings=("Device MAC", "Joined SSID", "BSSID", "Last Seen"),
            widths=(160, 220, 160, 120),
        )
        self.alerts_tree = self._make_tree(
            self.tabview.tab("Alerts"),
            columns=("age", "kind", "mac1", "mac2"),
            headings=("Age", "Type", "MAC 1", "MAC 2"),
            widths=(100, 100, 160, 160),
        )
        self.alerts_tree.tag_configure("deauth", foreground=COLOR_RED)
        self.alerts_tree.tag_configure("disassoc", foreground=COLOR_YELLOW)
        self.alerts_tree.tag_configure("reauth", foreground=COLOR_PINK)

        self._build_log()

    def _style_treeview(self):
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("Treeview",
                         background=COLOR_PANEL_BG, foreground=COLOR_TEXT,
                         fieldbackground=COLOR_PANEL_BG, rowheight=26, borderwidth=0,
                         font=FONT_MONO)
        style.configure("Treeview.Heading",
                         background="#1a2233", foreground=COLOR_YELLOW, relief="flat",
                         font=FONT_MONO_SMALL_BOLD)
        style.map("Treeview", background=[("selected", "#1f6aa5")])

    def _make_tree(self, parent, columns, headings, widths):
        tree = ttk.Treeview(parent, columns=columns, show="headings")
        for col, head, width in zip(columns, headings, widths):
            tree.heading(col, text=head)
            tree.column(col, width=width, anchor="w")
        tree.pack(fill="both", expand=True, padx=5, pady=5)
        return tree

    def _build_networks_canvas(self):
        frame = self.tabview.tab("Networks")
        self.networks_canvas = tk.Canvas(frame, bg=COLOR_BG, highlightthickness=0)
        self.networks_canvas.pack(fill="both", expand=True, padx=5, pady=5)
        self.networks_canvas.bind("<Configure>", lambda e: self._redraw_networks_canvas())

    def _build_log(self):
        log_frame = tk.Frame(self.tabview.tab("Log"), bg=COLOR_BG)
        log_frame.pack(fill="both", expand=True, padx=5, pady=5)
        self.log_box = tk.Text(log_frame, wrap="none", bg=COLOR_PANEL_BG, fg=COLOR_TEXT,
                                insertbackground=COLOR_TEXT, font=FONT_MONO,
                                borderwidth=0, highlightthickness=0)
        self.log_box.pack(fill="both", expand=True)
        self.log_box.tag_configure("error", foreground=COLOR_RED)
        self.log_box.tag_configure("info", foreground=COLOR_TEXT_DIM)

    # ------------------------------------------------------------------
    # Serial connection handling
    # ------------------------------------------------------------------

    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_menu.configure(values=ports or ["No ports found"])
        if ports:
            self.port_var.set(ports[0])

    def _toggle_connection(self):
        if self.reader_thread and self.reader_thread.is_alive():
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self.port_var.get()
        if not port or port == "No ports found":
            self.status_label.configure(text="○ Pick a port first", text_color=COLOR_YELLOW)
            return
        self.stop_event.clear()
        self.reader_thread = SerialReader(port, BAUD_RATE, self.line_queue, self.stop_event)
        self.reader_thread.start()
        self.status_label.configure(text=f"● Connected: {port} @ {BAUD_RATE}", text_color=COLOR_GREEN)
        self.connect_btn.configure(text="Disconnect", fg_color=COLOR_RED, hover_color="#c04850")

    def _disconnect(self):
        self.stop_event.set()
        if self.reader_thread:
            self.reader_thread.join(timeout=2)
        self.reader_thread = None
        self.status_label.configure(text="○ Disconnected", text_color=COLOR_RED)
        self.connect_btn.configure(text="Connect", fg_color=COLOR_GREEN, hover_color="#3fb877")

    # ------------------------------------------------------------------
    # Line parsing / rendering
    # ------------------------------------------------------------------

    def _poll_serial_queue(self):
        try:
            while True:
                line = self.line_queue.get_nowait()
                self._handle_line(line)
        except queue.Empty:
            pass
        self.after(POLL_INTERVAL_MS, self._poll_serial_queue)

    def _handle_line(self, line: str):
        if line.startswith("__ERROR__|"):
            self.status_label.configure(text="○ Connection error", text_color=COLOR_RED)
            self._log(line.split("|", 1)[1], tag="error")
            return

        if line == "BEGIN":
            self._pending_networks = []
            self._pending_associations = []
            self._pending_alerts = []
            return

        if line.startswith("END|"):
            parts = line.split("|")
            if len(parts) == 3:
                self._commit_batch(int(parts[1]), int(parts[2]))
            return

        if line.startswith("NET|"):
            parts = line.split("|")
            if len(parts) == 6:
                self._pending_networks.append(parts[1:])
            return

        if line.startswith("ASSOC|"):
            parts = line.split("|")
            if len(parts) == 5:
                self._pending_associations.append(parts[1:])
            return

        if line.startswith("ALERT|"):
            parts = line.split("|")
            if len(parts) == 5:
                self._pending_alerts.append(parts[1:])
            return

        # boot messages, "beacon" command feedback, or any unrecognized line
        self._log(line, tag="info")

    def _commit_batch(self, uptime_sec: int, total_alerts: int):
        self._current_networks = []
        for ch, ssid, rssi, secured, bssid in self._pending_networks:
            self._current_networks.append({
                "channel": int(ch),
                "ssid": ssid,
                "rssi": int(rssi),
                "secured": secured == "1",
                "bssid": bssid,
            })
        self._redraw_networks_canvas()

        self.devices_tree.delete(*self.devices_tree.get_children())
        for mac, ssid, bssid, last in self._pending_associations:
            self.devices_tree.insert("", "end", values=(mac, ssid, bssid, f"{last}s ago"))

        self.alerts_tree.delete(*self.alerts_tree.get_children())
        for kind, mac1, mac2, age in self._pending_alerts:
            self.alerts_tree.insert("", "end", values=(f"{age}s ago", kind, mac1, mac2 or "-"),
                                     tags=(kind.lower(),))

        hrs, rem = divmod(uptime_sec, 3600)
        mins, secs = divmod(rem, 60)
        self.uptime_label.configure(
            text=f"Uptime {hrs:02d}:{mins:02d}:{secs:02d}   Total alerts: {total_alerts}")

    # ------------------------------------------------------------------
    # Networks canvas: RSSI (y) vs channel (x) scatter with compact cards
    # ------------------------------------------------------------------

    def _redraw_networks_canvas(self):
        canvas = self.networks_canvas
        canvas.delete("all")
        w = canvas.winfo_width()
        h = canvas.winfo_height()
        if w < 100 or h < 100:
            return

        left_margin, right_margin = 55, 20
        top_margin, bottom_margin = 20, 30
        plot_w = w - left_margin - right_margin
        plot_h = h - top_margin - bottom_margin
        if plot_w <= 0 or plot_h <= 0:
            return

        # RSSI gridlines (every 10 dBm) with axis labels
        rssi_val = RSSI_MAX
        while rssi_val >= RSSI_MIN:
            frac = (RSSI_MAX - rssi_val) / (RSSI_MAX - RSSI_MIN)
            y = top_margin + frac * plot_h
            canvas.create_line(left_margin, y, w - right_margin, y, fill=COLOR_GRID)
            canvas.create_text(left_margin - 8, y, text=str(rssi_val), fill=COLOR_TEXT_DIM,
                                font=FONT_MONO_SMALL, anchor="e")
            rssi_val -= 10

        def channel_to_x(ch):
            return left_margin + ((ch - 1) / 12) * plot_w

        def rssi_to_y(rssi):
            clamped = max(RSSI_MIN, min(RSSI_MAX, rssi))
            frac = (RSSI_MAX - clamped) / (RSSI_MAX - RSSI_MIN)
            return top_margin + frac * plot_h

        # Channel gridlines + labels
        for ch in range(1, 14):
            x = channel_to_x(ch)
            canvas.create_line(x, top_margin, x, h - bottom_margin, fill=COLOR_GRID)
            canvas.create_text(x, h - bottom_margin + 14, text=str(ch), fill=COLOR_TEXT_DIM,
                                font=FONT_MONO_SMALL, anchor="n")

        box_w, box_h = 128, 46
        placed = []  # (x0, y0, x1, y1)

        def overlaps(bx0, by0, bx1, by1):
            return any(bx0 < px1 and bx1 > px0 and by0 < py1 and by1 > py0
                       for (px0, py0, px1, py1) in placed)

        for net in sorted(self._current_networks, key=lambda n: -n["rssi"]):
            cx = channel_to_x(net["channel"])
            cy = rssi_to_y(net["rssi"])

            box_x = max(left_margin, min(w - right_margin - box_w, cx - box_w / 2))
            box_y = cy - box_h / 2

            shift, attempts = 0, 0
            while overlaps(box_x, box_y + shift, box_x + box_w, box_y + box_h + shift) and attempts < 40:
                shift += box_h + 6
                attempts += 1
            final_y = box_y + shift
            placed.append((box_x, final_y, box_x + box_w, final_y + box_h))

            box_center = (box_x + box_w / 2, final_y + box_h / 2)
            if abs(box_center[1] - cy) > 4 or abs(box_center[0] - cx) > 4:
                canvas.create_line(cx, cy, box_center[0], box_center[1], fill=COLOR_PANEL_BORDER)

            accent = COLOR_BLUE if net["secured"] else COLOR_GREEN
            canvas.create_oval(cx - 3, cy - 3, cx + 3, cy + 3, fill=accent, outline="")
            canvas.create_rectangle(box_x, final_y, box_x + box_w, final_y + box_h,
                                     fill=COLOR_PANEL_BG, outline=accent, width=1.5)

            ssid = net["ssid"]
            ssid_display = ssid if len(ssid) <= 17 else ssid[:16] + "…"
            canvas.create_text(box_x + 6, final_y + 5, text=ssid_display, fill=COLOR_TEXT,
                                font=FONT_MONO_SMALL_BOLD, anchor="nw")
            canvas.create_text(box_x + 6, final_y + 20, text=net["bssid"], fill=COLOR_TEXT_DIM,
                                font=FONT_MONO_SMALL, anchor="nw")
            badge = ("SEC" if net["secured"] else "OPEN") + f"  {net['rssi']} dBm  ch{net['channel']}"
            canvas.create_text(box_x + 6, final_y + 33, text=badge, fill=accent,
                                font=FONT_MONO_SMALL, anchor="nw")

        self._draw_legend(canvas, w, top_margin, right_margin)

    def _draw_legend(self, canvas, w, top_margin, right_margin):
        lx = w - right_margin - 100
        ly = top_margin
        canvas.create_rectangle(lx - 8, ly - 6, w - right_margin + 4, ly + 42,
                                 fill=COLOR_PANEL_BG, outline=COLOR_PANEL_BORDER)
        canvas.create_oval(lx, ly, lx + 8, ly + 8, fill=COLOR_GREEN, outline="")
        canvas.create_text(lx + 14, ly + 4, text="Open", fill=COLOR_TEXT,
                            font=FONT_MONO_SMALL, anchor="w")
        canvas.create_oval(lx, ly + 18, lx + 8, ly + 26, fill=COLOR_BLUE, outline="")
        canvas.create_text(lx + 14, ly + 22, text="Secured", fill=COLOR_TEXT,
                            font=FONT_MONO_SMALL, anchor="w")

    def _log(self, text: str, tag: str = "info"):
        self.log_box.insert("end", text + "\n", tag)
        self.log_box.see("end")

    # ------------------------------------------------------------------

    def on_close(self):
        self._disconnect()
        self.destroy()


if __name__ == "__main__":
    app = WifiAnalyzerApp()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()
