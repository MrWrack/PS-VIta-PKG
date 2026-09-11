import os
import json
import zipfile
import shutil
import socket
import struct
import tempfile
import threading
import tkinter as tk
from tkinter import filedialog, colorchooser, messagebox
from PIL import Image, ImageTk, ImageOps

APP_W, APP_H = 960, 544
THEME_MAGIC = 0x54484D31  # THM1
THEME_PORT = 1339


class ThemeCreator(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('VPK Manager Theme Creator v2')
        self.geometry('1160x720')
        self.minsize(1040, 660)
        self.bg_path = None
        self.preview_img = None
        self.colors = {
            'bg': '#071018',
            'accent': '#00FF88',
            'panel': '#101820',
            'text': '#FFFFFF',
            'selected': '#22CCFF'
        }
        self._build()
        self._refresh_preview()

    def _build(self):
        left = tk.Frame(self, padx=16, pady=16)
        left.pack(side='left', fill='y')
        right = tk.Frame(self, padx=16, pady=16)
        right.pack(side='right', fill='both', expand=True)

        tk.Label(left, text='VPK Manager Theme Creator v2', font=('Segoe UI', 18, 'bold')).pack(anchor='w', pady=(0, 12))
        tk.Button(left, text='Välj bakgrund (PNG/JPG)', command=self.pick_bg, width=30).pack(anchor='w', pady=4)

        for key, label in [
            ('bg', 'Bakgrundsfärg'),
            ('accent', 'Accentfärg'),
            ('panel', 'Panelfärg'),
            ('text', 'Textfärg'),
            ('selected', 'Markeringsfärg')
        ]:
            row = tk.Frame(left)
            row.pack(fill='x', pady=4)
            tk.Label(row, text=label, width=16, anchor='w').pack(side='left')
            tk.Button(row, text='Välj', command=lambda k=key: self.pick_color(k), width=10).pack(side='left')

        tk.Label(left, text='Temanamn').pack(anchor='w', pady=(14, 2))
        self.name_var = tk.StringVar(value='My Custom Theme')
        tk.Entry(left, textvariable=self.name_var, width=31).pack(anchor='w')

        tk.Label(left, text='Skapare').pack(anchor='w', pady=(8, 2))
        self.author_var = tk.StringVar(value='MrWrack')
        tk.Entry(left, textvariable=self.author_var, width=31).pack(anchor='w')

        tk.Label(left, text='PS Vita IP').pack(anchor='w', pady=(16, 2))
        self.ip_var = tk.StringVar(value='192.168.1.50')
        tk.Entry(left, textvariable=self.ip_var, width=31).pack(anchor='w')
        tk.Label(left, text=f'Temaport: {THEME_PORT}', fg='#666').pack(anchor='w', pady=(2, 6))

        self.send_btn = tk.Button(left, text='Send Theme to PS Vita', command=self.send_theme, width=30)
        self.send_btn.pack(anchor='w', pady=(4, 10))
        self.status_var = tk.StringVar(value='Redo.')
        tk.Label(left, textvariable=self.status_var, justify='left', wraplength=260, fg='#1769aa').pack(anchor='w', pady=(0, 10))

        tk.Button(left, text='Exportera temamapp', command=self.export_folder, width=30).pack(anchor='w', pady=3)
        tk.Button(left, text='Exportera .zip', command=self.export_zip, width=30).pack(anchor='w', pady=3)
        tk.Button(left, text='Skapa Vita-färdigt paket', command=self.export_vita_package, width=30).pack(anchor='w', pady=3)

        tk.Label(left, text='På Vita: tryck L först.\nMål: ux0:/data/vpk_manager/theme/', justify='left', fg='#666').pack(anchor='w', pady=(14, 0))

        tk.Label(right, text='Förhandsvisning 960×544', font=('Segoe UI', 14, 'bold')).pack(anchor='w', pady=(0, 8))
        self.canvas = tk.Canvas(right, width=APP_W, height=APP_H, bg='black', highlightthickness=1)
        self.canvas.pack(fill='both', expand=True)

    def pick_bg(self):
        p = filedialog.askopenfilename(filetypes=[('Bilder', '*.png *.jpg *.jpeg')])
        if p:
            self.bg_path = p
            self._refresh_preview()

    def pick_color(self, key):
        c = colorchooser.askcolor(color=self.colors[key])[1]
        if c:
            self.colors[key] = c
            self._refresh_preview()

    def _compose(self):
        if self.bg_path and os.path.exists(self.bg_path):
            img = Image.open(self.bg_path).convert('RGB')
            img = ImageOps.fit(img, (APP_W, APP_H), method=Image.Resampling.LANCZOS)
        else:
            img = Image.new('RGB', (APP_W, APP_H), self.colors['bg'])
        return img

    def _refresh_preview(self):
        img = self._compose().resize((768, 435))
        self.preview_img = ImageTk.PhotoImage(img)
        self.canvas.delete('all')
        self.canvas.create_image(0, 0, anchor='nw', image=self.preview_img)
        self.canvas.create_rectangle(32, 32, 736, 96, fill=self.colors['panel'], outline=self.colors['accent'], width=3)
        self.canvas.create_text(52, 64, anchor='w', text='VPK MANAGER  v5', fill=self.colors['accent'], font=('Segoe UI', 22, 'bold'))
        self.canvas.create_rectangle(70, 145, 700, 330, fill=self.colors['panel'], outline=self.colors['selected'], width=2)
        self.canvas.create_text(95, 185, anchor='w', text='Installera VPK', fill=self.colors['text'], font=('Segoe UI', 18, 'bold'))
        self.canvas.create_text(95, 230, anchor='w', text='PC Quick Install', fill=self.colors['text'], font=('Segoe UI', 16))
        self.canvas.create_text(95, 275, anchor='w', text='Receive Theme (L)', fill=self.colors['text'], font=('Segoe UI', 16))
        self.canvas.create_text(95, 315, anchor='w', text='Inställningar', fill=self.colors['text'], font=('Segoe UI', 16))

    def _write_theme(self, out_dir):
        os.makedirs(out_dir, exist_ok=True)
        self._compose().save(os.path.join(out_dir, 'background.png'), 'PNG')
        name = self.name_var.get().strip() or 'Custom Theme'
        author = self.author_var.get().strip() or 'Unknown'
        with open(os.path.join(out_dir, 'theme.ini'), 'w', encoding='utf-8') as f:
            f.write(f'name={name}\n')
            f.write(f'author={author}\n')
            for k in ('bg', 'panel', 'accent', 'text', 'selected'):
                f.write(f'{k}={self.colors[k]}\n')
            f.write('background=background.png\n')
        with open(os.path.join(out_dir, 'theme.json'), 'w', encoding='utf-8') as f:
            json.dump({'name': name, 'author': author, 'colors': self.colors, 'background': 'background.png'}, f, indent=2)

    def _make_zip(self, zip_path):
        with tempfile.TemporaryDirectory(prefix='vpk_theme_') as tmp:
            self._write_theme(tmp)
            with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
                for fn in ('background.png', 'theme.ini', 'theme.json'):
                    z.write(os.path.join(tmp, fn), fn)

    def send_theme(self):
        ip = self.ip_var.get().strip()
        if not ip:
            messagebox.showerror('IP saknas', 'Skriv in PS Vita-IP:n som visas i VPK Manager.')
            return
        self.send_btn.config(state='disabled')
        self.status_var.set('Förbereder tema...')
        threading.Thread(target=self._send_worker, args=(ip,), daemon=True).start()

    def _set_status(self, text):
        self.after(0, lambda: self.status_var.set(text))

    def _send_worker(self, ip):
        tmp_zip = None
        try:
            fd, tmp_zip = tempfile.mkstemp(prefix='vpk_manager_theme_', suffix='.zip')
            os.close(fd)
            self._make_zip(tmp_zip)
            size = os.path.getsize(tmp_zip)
            name = b'vpk_manager_theme.zip'
            header = struct.pack('<IIQ', THEME_MAGIC, len(name), size)

            self._set_status(f'Ansluter till {ip}:{THEME_PORT}...')
            with socket.create_connection((ip, THEME_PORT), timeout=12) as sock:
                sock.sendall(header)
                sock.sendall(name)
                sent = 0
                with open(tmp_zip, 'rb') as f:
                    while True:
                        chunk = f.read(64 * 1024)
                        if not chunk:
                            break
                        sock.sendall(chunk)
                        sent += len(chunk)
                        pct = int(sent * 100 / max(size, 1))
                        self._set_status(f'Skickar tema... {pct}%')
                reply = sock.recv(1)
                if reply != b'\x01':
                    raise RuntimeError('Vita rapporterade att mottagningen misslyckades.')
            self._set_status('Klart! Temat skickades och aktiverades på PS Vita.')
            self.after(0, lambda: messagebox.showinfo('Klart', 'Temat skickades till PS Vita och ska nu vara aktiverat.'))
        except Exception as e:
            self._set_status(f'Fel: {e}')
            self.after(0, lambda: messagebox.showerror('Överföringsfel', f'Kunde inte skicka temat.\n\n{e}\n\nKontrollera att Vita och PC är på samma Wi-Fi och att du tryckt L i VPK Manager först.'))
        finally:
            if tmp_zip and os.path.exists(tmp_zip):
                try:
                    os.remove(tmp_zip)
                except OSError:
                    pass
            self.after(0, lambda: self.send_btn.config(state='normal'))

    def export_folder(self):
        d = filedialog.askdirectory(title='Välj exportmapp')
        if not d:
            return
        out = os.path.join(d, 'vpk_manager_theme')
        if os.path.exists(out):
            shutil.rmtree(out)
        self._write_theme(out)
        messagebox.showinfo('Klart', f'Temat exporterades till:\n{out}')

    def export_zip(self):
        p = filedialog.asksaveasfilename(defaultextension='.zip', filetypes=[('ZIP', '*.zip')], initialfile='vpk_manager_theme.zip')
        if not p:
            return
        self._make_zip(p)
        messagebox.showinfo('Klart', f'ZIP skapad:\n{p}')

    def export_vita_package(self):
        d = filedialog.askdirectory(title='Välj exportmapp')
        if not d:
            return
        out = os.path.join(d, 'ux0', 'data', 'vpk_manager', 'theme')
        self._write_theme(out)
        messagebox.showinfo('Klart', 'Vita-färdig mappstruktur skapad. Kopiera mappen ux0 till Vita via FTP/USB.')


if __name__ == '__main__':
    ThemeCreator().mainloop()
