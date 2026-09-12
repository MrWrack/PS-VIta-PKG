#!/usr/bin/env python3
import json, os, socket, struct, threading, tkinter as tk, zipfile, tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from tkinter import filedialog, messagebox, ttk
from pathlib import Path
from PIL import Image, ImageTk

VPK_MAGIC=0x56504B31; THEME_MAGIC=0x54484D31
DEFAULT_VPK_PORT=1338; DEFAULT_THEME_PORT=1339; EXAMPLE_IP='192.102.1.175'
APP_DIR=Path(os.getenv('APPDATA',Path.home()))/'VPKManagerPC'; PROFILE_FILE=APP_DIR/'profiles.json'

class App(tk.Tk):
 def __init__(self):
  super().__init__(); self.title('VPK Manager PC'); self.geometry('780x690'); self.minsize(740,650)
  self.sock=None; self.vpk_path=''; self.theme_path=''; self.preview_ref=None; self.profiles=[]; self.default_index=-1
  self._build(); self._load_profiles()
 def _build(self):
  root=ttk.Frame(self,padding=16); root.pack(fill='both',expand=True)
  ttk.Label(root,text='VPK Manager PC',font=('Segoe UI',20,'bold')).pack(anchor='w')
  ttk.Label(root,text='VPK Quick Install + Theme Change för PS Vita').pack(anchor='w',pady=(0,12))
  pf=ttk.LabelFrame(root,text='PS Vita-anslutning',padding=10); pf.pack(fill='x')
  r=ttk.Frame(pf);r.pack(fill='x');ttk.Label(r,text='Sparad Vita:').pack(side='left');self.combo=ttk.Combobox(r,state='readonly',width=28);self.combo.pack(side='left',padx=8,fill='x',expand=True);self.combo.bind('<<ComboboxSelected>>',lambda e:self._select_profile())
  for t,c in [('Lägg till',self._add),('Ändra',self._edit),('Ta bort',self._delete),('Standard',self._set_default)]:ttk.Button(r,text=t,command=c).pack(side='left',padx=2)
  r=ttk.Frame(pf);r.pack(fill='x',pady=(9,0));self.ip=tk.StringVar(value=EXAMPLE_IP);self.vpk_port=tk.StringVar(value='1338');self.theme_port=tk.StringVar(value='1339')
  ttk.Label(r,text='IP:').pack(side='left');ttk.Entry(r,textvariable=self.ip,width=18).pack(side='left',padx=(4,10));ttk.Label(r,text='VPK:').pack(side='left');ttk.Entry(r,textvariable=self.vpk_port,width=6).pack(side='left',padx=4);ttk.Label(r,text='Theme:').pack(side='left');ttk.Entry(r,textvariable=self.theme_port,width=6).pack(side='left',padx=4)
  self.connect_btn=ttk.Button(r,text='Connect',command=self._connect);self.connect_btn.pack(side='right');self.scan_btn=ttk.Button(r,text='Sök PS Vita',command=self._scan);self.scan_btn.pack(side='right',padx=7)
  vf=ttk.LabelFrame(root,text='VPK',padding=10);vf.pack(fill='x',pady=(12,8));r=ttk.Frame(vf);r.pack(fill='x');self.file_label=ttk.Label(r,text='Ingen VPK vald');self.file_label.pack(side='left',fill='x',expand=True);ttk.Button(r,text='Välj VPK...',command=self._pick_vpk).pack(side='right');ttk.Button(vf,text='Send VPK',command=self._send_vpk).pack(anchor='e',pady=(7,0))
  tf=ttk.LabelFrame(root,text='Theme Change',padding=10);tf.pack(fill='both',expand=True,pady=6);r=ttk.Frame(tf);r.pack(fill='x');self.theme_label=ttk.Label(r,text='Ingen bakgrund vald');self.theme_label.pack(side='left',fill='x',expand=True);ttk.Button(r,text='Välj bakgrund...',command=self._pick_theme).pack(side='right')
  self.preview=tk.Label(tf,text='Preview 960 × 544',height=10);self.preview.pack(fill='both',expand=True,pady=8)
  r=ttk.Frame(tf);r.pack(fill='x');ttk.Button(r,text='Reset Default',command=self._reset_info).pack(side='left');ttk.Button(r,text='Send Theme',command=self._send_theme).pack(side='right')
  self.progress=ttk.Progressbar(root,maximum=100);self.progress.pack(fill='x',pady=(8,4));self.status=tk.StringVar(value='Redo');ttk.Label(root,textvariable=self.status).pack(anchor='w')
 def _load_profiles(self):
  try:d=json.loads(PROFILE_FILE.read_text(encoding='utf8'));self.profiles=d.get('profiles',[]);self.default_index=d.get('default',-1)
  except:self.profiles=[];self.default_index=-1
  for p in self.profiles:p.setdefault('vpk_port',p.pop('port',1338));p.setdefault('theme_port',1339)
  self._refresh();
  if 0<=self.default_index<len(self.profiles):self.combo.current(self.default_index);self._select_profile()
 def _save(self):APP_DIR.mkdir(parents=True,exist_ok=True);PROFILE_FILE.write_text(json.dumps({'profiles':self.profiles,'default':self.default_index},indent=2),encoding='utf8')
 def _refresh(self):self.combo['values']=[('★ ' if i==self.default_index else '')+p['name'] for i,p in enumerate(self.profiles)]
 def _dialog(self,title,p=None):
  p=p or {};w=tk.Toplevel(self);w.title(title);w.transient(self);w.grab_set();vals={k:tk.StringVar(value=str(p.get(k,v))) for k,v in [('name','Min PS Vita'),('ip',EXAMPLE_IP),('vpk_port',1338),('theme_port',1339)]}
  for i,(lab,k) in enumerate([('Namn','name'),('IP-adress','ip'),('VPK-port','vpk_port'),('Theme-port','theme_port')]):ttk.Label(w,text=lab).grid(row=i,column=0,padx=10,pady=6,sticky='w');ttk.Entry(w,textvariable=vals[k],width=30).grid(row=i,column=1,padx=10,pady=6)
  out=[]
  def ok():
   try:socket.inet_aton(vals['ip'].get());vp=int(vals['vpk_port'].get());tp=int(vals['theme_port'].get())
   except:messagebox.showerror('Fel','Kontrollera IP och portar.',parent=w);return
   out.append({'name':vals['name'].get() or 'PS Vita','ip':vals['ip'].get(),'vpk_port':vp,'theme_port':tp});w.destroy()
  ttk.Button(w,text='Spara',command=ok).grid(row=4,column=1,pady=10,sticky='e');self.wait_window(w);return out[0] if out else None
 def _add(self):
  p=self._dialog('Lägg till PS Vita');
  if p:self.profiles.append(p);self._save();self._refresh();self.combo.current(len(self.profiles)-1);self._select_profile()
 def _edit(self):
  i=self.combo.current();
  if i>=0:
   p=self._dialog('Ändra PS Vita',self.profiles[i]);
   if p:self.profiles[i]=p;self._save();self._refresh();self.combo.current(i);self._select_profile()
 def _delete(self):
  i=self.combo.current();
  if i>=0 and messagebox.askyesno('Ta bort','Ta bort sparad PS Vita?'):self.profiles.pop(i);self.default_index=-1 if self.default_index==i else max(-1,self.default_index-(self.default_index>i));self._save();self._refresh()
 def _set_default(self):
  i=self.combo.current();
  if i>=0:self.default_index=i;self._save();self._refresh();self.combo.current(i);self.status.set('Standard-Vita sparad.')
 def _select_profile(self):
  i=self.combo.current();
  if i>=0:p=self.profiles[i];self.ip.set(p['ip']);self.vpk_port.set(p['vpk_port']);self.theme_port.set(p['theme_port'])
 def _local_ip(self):
  s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
  try:s.connect(('8.8.8.8',80));return s.getsockname()[0]
  finally:s.close()
 def _scan(self):
  try:local=self._local_ip();prefix='.'.join(local.split('.')[:3])+'.'
  except Exception as e:messagebox.showerror('Sök PS Vita',str(e));return
  self.scan_btn.config(state='disabled');self.status.set(f'Söker PS Vita på {prefix}0/24...')
  def probe(h):
   s=socket.socket();s.settimeout(.3)
   try:return h if s.connect_ex((h,1338))==0 else None
   finally:s.close()
  def work():
   found=None
   with ThreadPoolExecutor(max_workers=48) as ex:
    for f in as_completed([ex.submit(probe,prefix+str(i)) for i in range(1,255)]):
     if f.result():found=f.result();break
   self.after(0,lambda:self._scan_done(found))
  threading.Thread(target=work,daemon=True).start()
 def _scan_done(self,h):
  self.scan_btn.config(state='normal')
  if h:self.ip.set(h);self.vpk_port.set('1338');self.theme_port.set('1339');self.status.set(f'VPK Manager hittad: {h} | VPK 1338 | Theme 1339')
  else:self.status.set('Ingen PS Vita hittades.')
 def _connect(self):
  if self.sock:
   try:self.sock.close()
   except:pass
   self.sock=None;self.connect_btn.config(text='Connect');self.status.set('Frånkopplad.');return
  self.status.set('Ansluter...')
  def w():
   try:self.sock=socket.create_connection((self.ip.get(),int(self.vpk_port.get())),timeout=5);self.after(0,lambda:(self.connect_btn.config(text='Disconnect'),self.status.set('Ansluten till PS Vita.')))
   except Exception as e:self.after(0,lambda:self.status.set('Kunde inte ansluta: '+str(e)))
  threading.Thread(target=w,daemon=True).start()
 def _pick_vpk(self):
  p=filedialog.askopenfilename(filetypes=[('PS Vita VPK','*.vpk')]);
  if p:self.vpk_path=p;self.file_label.config(text=os.path.basename(p))
 def _pick_theme(self):
  p=filedialog.askopenfilename(filetypes=[('Bilder','*.png *.jpg *.jpeg')]);
  if not p:return
  self.theme_path=p;self.theme_label.config(text=os.path.basename(p))
  try:
   im=Image.open(p).convert('RGB');im.thumbnail((430,245));self.preview_ref=ImageTk.PhotoImage(im);self.preview.config(image=self.preview_ref,text='')
  except Exception as e:messagebox.showerror('Theme',str(e))
 def _send_file(self,path,magic,port,label,done):
  self.progress['value']=0
  def w():
   try:
    s=socket.create_connection((self.ip.get(),int(port)),timeout=8);s.settimeout(30);name=os.path.basename(path).encode();size=os.path.getsize(path);s.sendall(struct.pack('<IIQ',magic,len(name),size)+name);sent=0
    with open(path,'rb') as f:
     while True:
      b=f.read(262144)
      if not b:break
      s.sendall(b);sent+=len(b);pct=int(sent*100/size);self.after(0,lambda p=pct:self._prog(label,p))
    ok=s.recv(1);s.close()
    if ok!=b'\x01':raise RuntimeError('PS Vita rapporterade fel')
    self.after(0,lambda:self.status.set(done))
   except Exception as e:self.after(0,lambda:self.status.set('Fel: '+str(e)))
  threading.Thread(target=w,daemon=True).start()
 def _prog(self,l,p):self.progress['value']=p;self.status.set(f'{l}: {p}%')
 def _send_vpk(self):
  if not self.vpk_path:return messagebox.showwarning('VPK','Välj en VPK först.')
  # use existing connection if available, otherwise receiver may have been consumed by discovery; reconnect cleanly
  if self.sock:
   try:self.sock.close()
   except:pass
   self.sock=None;self.connect_btn.config(text='Connect')
  self._send_file(self.vpk_path,VPK_MAGIC,self.vpk_port.get(),'Skickar VPK','Klar! VPK mottagen av PS Vita.')
 def _theme_zip(self):
  td=tempfile.mkdtemp(prefix='vpkm_theme_');out=os.path.join(td,'theme.zip');im=Image.open(self.theme_path).convert('RGBA').resize((960,544),Image.Resampling.LANCZOS);bg=os.path.join(td,'background.png');im.save(bg,'PNG');ini=os.path.join(td,'theme.ini');open(ini,'w',encoding='utf8').write('name=PC Custom Theme\nbg=#08100B\npanel=#122619\naccent=#50FF8C\ntext=#F5F5F5\nselected=#285037\n');
  with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:z.write(bg,'background.png');z.write(ini,'theme.ini')
  return out
 def _send_theme(self):
  if not self.theme_path:return messagebox.showwarning('Theme','Välj en bakgrund först.')
  try:z=self._theme_zip()
  except Exception as e:return messagebox.showerror('Theme',str(e))
  self._send_file(z,THEME_MAGIC,self.theme_port.get(),'Skickar Theme','Klar! Theme mottaget och aktiverat på PS Vita.')
 def _reset_info(self):messagebox.showinfo('Reset Default','På PS Vita: START → välj Green/Default. Den inbyggda MrWrack-bakgrunden finns alltid kvar och skrivs inte över.')
if __name__=='__main__':App().mainloop()
