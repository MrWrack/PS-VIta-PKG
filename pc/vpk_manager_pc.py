#!/usr/bin/env python3
import json, os, socket, struct, threading, tkinter as tk
from tkinter import filedialog, messagebox, ttk
from pathlib import Path

MAGIC = 0x56504B31
DEFAULT_PORT = 1338
EXAMPLE_IP = "192.102.1.175"
APP_DIR = Path(os.getenv("APPDATA", Path.home())) / "VPKManagerPC"
PROFILE_FILE = APP_DIR / "profiles.json"

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("VPK Manager PC")
        self.geometry("720x470")
        self.minsize(680, 430)
        self.sock = None
        self.vpk_path = ""
        self.profiles = []
        self.default_index = -1
        self._build()
        self._load_profiles()

    def _build(self):
        root = ttk.Frame(self, padding=16); root.pack(fill="both", expand=True)
        ttk.Label(root, text="VPK Manager PC", font=("Segoe UI", 20, "bold")).pack(anchor="w")
        ttk.Label(root, text="Skicka VPK-filer via Wi-Fi till VPK Manager på PS Vita.").pack(anchor="w", pady=(0,14))

        pf = ttk.LabelFrame(root, text="PS Vita-anslutning", padding=12); pf.pack(fill="x")
        row = ttk.Frame(pf); row.pack(fill="x")
        ttk.Label(row, text="Sparad Vita:").pack(side="left")
        self.combo = ttk.Combobox(row, state="readonly", width=30); self.combo.pack(side="left", padx=8, fill="x", expand=True)
        self.combo.bind("<<ComboboxSelected>>", lambda e:self._select_profile())
        ttk.Button(row, text="Lägg till", command=self._add).pack(side="left", padx=2)
        ttk.Button(row, text="Ändra", command=self._edit).pack(side="left", padx=2)
        ttk.Button(row, text="Ta bort", command=self._delete).pack(side="left", padx=2)
        ttk.Button(row, text="Standard", command=self._set_default).pack(side="left", padx=2)

        row2 = ttk.Frame(pf); row2.pack(fill="x", pady=(10,0))
        ttk.Label(row2, text="IP:").pack(side="left")
        self.ip = tk.StringVar(value=EXAMPLE_IP); ttk.Entry(row2, textvariable=self.ip, width=22).pack(side="left", padx=(5,14))
        ttk.Label(row2, text="Port:").pack(side="left")
        self.port = tk.StringVar(value=str(DEFAULT_PORT)); ttk.Entry(row2, textvariable=self.port, width=8).pack(side="left", padx=5)
        self.connect_btn = ttk.Button(row2, text="Connect", command=self._connect); self.connect_btn.pack(side="right")

        ff = ttk.LabelFrame(root, text="VPK", padding=12); ff.pack(fill="x", pady=14)
        r = ttk.Frame(ff); r.pack(fill="x")
        self.file_label = ttk.Label(r, text="Ingen VPK vald"); self.file_label.pack(side="left", fill="x", expand=True)
        ttk.Button(r, text="Välj VPK...", command=self._pick).pack(side="right")

        self.progress = ttk.Progressbar(root, maximum=100); self.progress.pack(fill="x", pady=(6,6))
        self.status = tk.StringVar(value="Redo")
        ttk.Label(root, textvariable=self.status).pack(anchor="w")
        self.send_btn = ttk.Button(root, text="Send VPK", command=self._send); self.send_btn.pack(anchor="e", pady=12)
        ttk.Label(root, text="På PS Vita: öppna VPK Manager. PC Quick Install startar automatiskt.").pack(anchor="w")

    def _load_profiles(self):
        try:
            d=json.loads(PROFILE_FILE.read_text(encoding="utf-8")); self.profiles=d.get("profiles",[]); self.default_index=d.get("default",-1)
        except Exception: self.profiles=[]; self.default_index=-1
        self._refresh_combo()
        if 0 <= self.default_index < len(self.profiles): self.combo.current(self.default_index); self._select_profile()

    def _save_profiles(self):
        APP_DIR.mkdir(parents=True, exist_ok=True)
        PROFILE_FILE.write_text(json.dumps({"profiles":self.profiles,"default":self.default_index}, indent=2), encoding="utf-8")

    def _refresh_combo(self):
        vals=[]
        for i,p in enumerate(self.profiles): vals.append(("★ " if i==self.default_index else "") + p["name"])
        self.combo["values"]=vals
        if not vals: self.combo.set("")

    def _profile_dialog(self, title, initial=None):
        win=tk.Toplevel(self); win.title(title); win.resizable(False,False); win.transient(self); win.grab_set()
        vals={"name":tk.StringVar(value=(initial or {}).get("name","Min PS Vita")),"ip":tk.StringVar(value=(initial or {}).get("ip",EXAMPLE_IP)),"port":tk.StringVar(value=str((initial or {}).get("port",DEFAULT_PORT)))}
        for n,(lab,key) in enumerate((("Namn","name"),("IP-adress","ip"),("Port","port"))):
            ttk.Label(win,text=lab).grid(row=n,column=0,padx=10,pady=7,sticky="w"); ttk.Entry(win,textvariable=vals[key],width=30).grid(row=n,column=1,padx=10,pady=7)
        result=[]
        def ok():
            try: port=int(vals["port"].get()); socket.inet_aton(vals["ip"].get().strip())
            except Exception: messagebox.showerror("Fel","Kontrollera IP-adress och port.",parent=win); return
            result.append({"name":vals["name"].get().strip() or "PS Vita","ip":vals["ip"].get().strip(),"port":port}); win.destroy()
        ttk.Button(win,text="Spara",command=ok).grid(row=3,column=1,padx=10,pady=10,sticky="e")
        self.wait_window(win); return result[0] if result else None

    def _add(self):
        p=self._profile_dialog("Lägg till PS Vita")
        if p: self.profiles.append(p); self._save_profiles(); self._refresh_combo(); self.combo.current(len(self.profiles)-1); self._select_profile()
    def _edit(self):
        i=self.combo.current()
        if i<0: return
        p=self._profile_dialog("Ändra PS Vita",self.profiles[i])
        if p: self.profiles[i]=p; self._save_profiles(); self._refresh_combo(); self.combo.current(i); self._select_profile()
    def _delete(self):
        i=self.combo.current()
        if i<0:return
        if messagebox.askyesno("Ta bort",f"Ta bort {self.profiles[i]['name']}?"):
            self.profiles.pop(i)
            if self.default_index==i:self.default_index=-1
            elif self.default_index>i:self.default_index-=1
            self._save_profiles(); self._refresh_combo()
            if self.profiles:self.combo.current(0);self._select_profile()
    def _set_default(self):
        i=self.combo.current()
        if i>=0:self.default_index=i;self._save_profiles();self._refresh_combo();self.combo.current(i);self.status.set("Standard-Vita sparad.")
    def _select_profile(self):
        i=self.combo.current()
        if i>=0:self.ip.set(self.profiles[i]["ip"]);self.port.set(str(self.profiles[i]["port"]))
    def _pick(self):
        p=filedialog.askopenfilename(title="Välj VPK",filetypes=[("PS Vita VPK","*.vpk"),("Alla filer","*.*")])
        if p:self.vpk_path=p;self.file_label.config(text=os.path.basename(p));self.progress["value"]=0

    def _disconnect(self):
        if self.sock:
            try:self.sock.close()
            except:pass
        self.sock=None
        self.connect_btn.config(text="Connect")
    def _connect(self):
        if self.sock:self._disconnect();self.status.set("Frånkopplad.");return
        try: port=int(self.port.get()); socket.inet_aton(self.ip.get().strip())
        except Exception: messagebox.showerror("Fel","Ogiltig IP-adress eller port.");return
        self.status.set("Ansluter..."); self.connect_btn.config(state="disabled")
        def work():
            try:
                s=socket.create_connection((self.ip.get().strip(),port),timeout=10);s.settimeout(20);self.sock=s
                self.after(0,lambda:(self.status.set("Ansluten till PS Vita."),self.connect_btn.config(text="Disconnect",state="normal")))
            except Exception as e:self.after(0,lambda:self._connect_error(str(e)))
        threading.Thread(target=work,daemon=True).start()
    def _connect_error(self,msg):self._disconnect();self.connect_btn.config(state="normal");self.status.set("Kunde inte ansluta: "+msg)

    def _send(self):
        if not self.vpk_path or not os.path.isfile(self.vpk_path):messagebox.showwarning("VPK","Välj en VPK-fil först.");return
        if not self.sock:messagebox.showwarning("Anslutning","Tryck Connect först och kontrollera att VPK Manager är öppet på Vitan.");return
        self.send_btn.config(state="disabled");self.progress["value"]=0
        s=self.sock; self.sock=None; self.connect_btn.config(text="Connect")
        path=self.vpk_path
        def work():
            try:
                name=os.path.basename(path).encode("utf-8");size=os.path.getsize(path)
                if len(name)>=240 or size<=0:raise ValueError("Ogiltigt filnamn eller tom VPK")
                s.sendall(struct.pack("<IIQ",MAGIC,len(name),size));s.sendall(name)
                sent=0
                with open(path,"rb") as f:
                    while True:
                        b=f.read(256*1024)
                        if not b:break
                        s.sendall(b);sent+=len(b);pct=int(sent*100/size)
                        self.after(0,lambda p=pct:self._progress(p))
                ok=s.recv(1)
                if ok!=b"\x01":raise RuntimeError("PS Vita rapporterade överföringsfel")
                self.after(0,lambda:self.status.set("Klar! VPK mottagen av PS Vita."))
            except Exception as e:self.after(0,lambda m=str(e):self.status.set("Fel: "+m))
            finally:
                try:s.close()
                except:pass
                self.after(0,lambda:self.send_btn.config(state="normal"))
        threading.Thread(target=work,daemon=True).start()
    def _progress(self,p):self.progress["value"]=p;self.status.set(f"Skickar VPK: {p}%")

if __name__=="__main__": App().mainloop()
