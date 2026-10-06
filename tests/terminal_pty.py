import os,pty,select,subprocess,time,fcntl,termios,struct,signal,json,re,codecs,shutil,zipfile
from pathlib import Path
import tempfile,atexit
if os.environ.get("LTREE_PTY_WORK"):
 work=Path(os.environ["LTREE_PTY_WORK"]);work.mkdir(parents=True,exist_ok=True)
else:
 work=Path(tempfile.mkdtemp(prefix="ltc-terminal-test-"))
 atexit.register(shutil.rmtree,work,True)
checks=[]
class Screen:
 def __init__(self,rows=32,cols=140):self.rows=rows;self.cols=cols;self.grid=[[' ']*cols for _ in range(rows)];self.r=0;self.c=0;self.state='';self.buf='';self.decode=codecs.getincrementaldecoder('utf8')('replace');self.last=' '
 def feed(self,data):
  for ch in self.decode.decode(data):
   if self.state=='osc':
    if ch=='\a':self.state=''
    elif ch=='\x1b':self.state='oscesc'
    continue
   if self.state=='oscesc':self.state='' if ch=='\\' else 'osc';continue
   if self.state=='esc':
    if ch=='[':self.state='csi';self.buf=''
    elif ch==']':self.state='osc'
    elif ch in '()':self.state='charset'
    else:self.state=''
    continue
   if self.state=='charset':self.state='';continue
   if self.state=='csi':
    if '@'<=ch<='~':
     nums=[int(n) if n.isdigit() else 0 for n in self.buf.lstrip('?=>').split(';')];n=nums[0] or 1
     if ch in 'Hf':self.r=(nums[0] or 1)-1;self.c=(nums[1] if len(nums)>1 and nums[1] else 1)-1
     elif ch=='A':self.r-=n
     elif ch=='B':self.r+=n
     elif ch=='C':self.c+=n
     elif ch=='D':self.c-=n
     elif ch=='G':self.c=n-1
     elif ch=='d':self.r=n-1
     elif ch=='J':
      if nums[0] in [2,3]:self.grid=[[' ']*self.cols for _ in range(self.rows)]
      elif nums[0]==0:
       self.grid[self.r][self.c:]=[' ']*(self.cols-self.c)
       for r in range(self.r+1,self.rows):self.grid[r]=[' ']*self.cols
     elif ch=='K':
      if nums[0]==2:self.grid[self.r]=[' ']*self.cols
      elif nums[0]==1:self.grid[self.r][:self.c+1]=[' ']*(self.c+1)
      else:self.grid[self.r][self.c:]=[' ']*(self.cols-self.c)
     elif ch=='P':self.grid[self.r][self.c:]=self.grid[self.r][self.c+n:]+[' ']*min(n,self.cols-self.c)
     elif ch=='@':self.grid[self.r][self.c:]=([' ']*n+self.grid[self.r][self.c:])[:self.cols-self.c]
     elif ch=='X':self.grid[self.r][self.c:self.c+n]=[' ']*min(n,self.cols-self.c)
     elif ch=='b':
      for _ in range(n):self.put(self.last)
     self.r=max(0,min(self.rows-1,self.r));self.c=max(0,min(self.cols-1,self.c));self.state=''
    else:self.buf+=ch
    continue
   if ch=='\x1b':self.state='esc'
   elif ch=='\r':self.c=0
   elif ch=='\n':self.r=min(self.rows-1,self.r+1)
   elif ch=='\b':self.c=max(0,self.c-1)
   elif ch=='\t':self.c=min(self.cols-1,(self.c//8+1)*8)
   elif ord(ch)>=32:self.put(ch)
 def put(self,ch):
  self.grid[self.r][min(self.c,self.cols-1)]=ch;self.last=ch;self.c=min(self.cols-1,self.c+1)
 def text(self):return '\n'.join(''.join(line).rstrip() for line in self.grid)
class App:
 def __init__(self,name,files,shared_runtime=None,terminal_args=None):
  self.dir=work/name;shutil.rmtree(self.dir,ignore_errors=True);self.dir.mkdir();self.root=self.dir/'root';self.root.mkdir();self.home=self.dir/'home';self.home.mkdir();self.runtime=Path(shared_runtime) if shared_runtime is not None else self.home/'runtime';self.runtime.mkdir(exist_ok=True);self.runtime.chmod(0o700)
  for name,content in files.items():p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(content)
  self.screen=Screen();self.transcript=bytearray();self.master,slave=pty.openpty();fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',32,140,0,0))
  env=os.environ.copy()
  for k in ['DISPLAY','WAYLAND_DISPLAY','QT_QPA_PLATFORM','LTREE_HISTORY_FILE']:env.pop(k,None)
  env.update(HOME=str(self.home),XDG_CONFIG_HOME=str(self.home/'config'),XDG_RUNTIME_DIR=str(self.runtime),HISTFILE=str(self.home/'history'),TERM='xterm-256color',LANG='C.UTF-8')
  def setup():os.setsid();fcntl.ioctl(0,termios.TIOCSCTTY,0)
  self.env=env
  args=['--terminal'] if terminal_args is None else terminal_args
  self.proc=subprocess.Popen([os.environ.get('LTC_TEST_BINARY','/home/hector/fuentes/ltree-commander/build/ltc')]+args+[str(self.root)],stdin=slave,stdout=slave,stderr=slave,env=env,preexec_fn=setup);os.close(slave);self.read(.5)
 def read(self,seconds=.15):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   if select.select([self.master],[],[],.02)[0]:
    try:data=os.read(self.master,65536)
    except OSError:break
    self.transcript.extend(data);self.screen.feed(data)
  return self.screen.text()
 def send(self,key,seconds=.15):os.write(self.master,key.encode() if isinstance(key,str) else key);return self.read(seconds)
 def wait(self,value,seconds=5):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   text=self.read(.05)
   if value in text:return text
   if self.proc.poll() is not None:break
  raise AssertionError('Missing '+value+'\n'+self.screen.text())
 def check(self,name,value):
  checks.append({'name':name,'passed':bool(value)})
  if not value:raise AssertionError(name+'\n'+self.screen.text())
 def close(self):
  self.dir.joinpath('transcript.bin').write_bytes(self.transcript);self.dir.joinpath('screen.txt').write_text(self.screen.text())
  if self.proc.poll() is None:os.killpg(self.proc.pid,signal.SIGTERM);self.proc.wait(timeout=5)
  os.close(self.master)
F1=b'\x1bOP';F3=b'\x1bOR';F4=b'\x1bOS';F5=b'\x1b[15~';F8=b'\x1b[19~';F10=b'\x1b[21~';UP=b'\x1bOA';DOWN=b'\x1bOB';END=b'\x1bOF';HOME=b'\x1bOH';LEFT=b'\x1bOD';RIGHT=b'\x1bOC'
def independent_instances():
 instances=[];gui=None;runtime=Path(tempfile.mkdtemp(prefix='ltc-instances-'));log=(work/'instances-gui.log').open('wb')
 try:
  first=App('instance-first',{'first-session':b'one'},shared_runtime=runtime);instances.append(first)
  second=App('instance-second',{'second-session':b'two'},shared_runtime=runtime,terminal_args=[]);instances.append(second)
  first.wait('first-session');second.wait('second-session')
  first.check('Two terminals share runtime without new-instance',first.proc.poll() is None and second.proc.poll() is None)
  first.check('Terminal sessions create no GUI lock or socket',not(runtime/'ltreec.lock').exists() and not(runtime/'ltreec.socket').exists())
  env=first.env.copy();env['QT_QPA_PLATFORM']='offscreen';binary=os.environ.get('LTC_TEST_BINARY','/home/hector/fuentes/ltree-commander/build/ltc')
  gui=subprocess.Popen([binary,str(first.root)],env=env,stdout=log,stderr=log)
  deadline=time.monotonic()+5
  while not(runtime/'ltreec.socket').exists() and gui.poll() is None and time.monotonic()<deadline:time.sleep(.02)
  first.check('GUI starts while terminals remain open',gui.poll() is None and (runtime/'ltreec.socket').exists())
  for name,args in [('instance-forced',['--terminal']),('instance-auto',[])]:
   app=App(name,{'own-session':b'own'},shared_runtime=runtime,terminal_args=args);instances.append(app);app.wait('own-session')
   app.check(name+' ignores existing GUI',app.proc.poll() is None and gui.poll() is None)
  activated=subprocess.run([binary,str(first.root)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=5)
  first.check('Graphical launch still activates existing window',activated.returncode==0 and b'Activated the existing' in activated.stdout)
  first.send('q');first.send('y');first.check('Closing one terminal keeps others running',first.proc.wait(timeout=5)==0 and all(app.proc.poll() is None for app in instances[1:]) and gui.poll() is None)
 finally:
  for app in instances:app.close()
  if gui is not None and gui.poll() is None:gui.terminate();gui.wait(timeout=5)
  log.close();shutil.rmtree(runtime)

def main():
 a=None
 try:
  independent_instances()
  a=App('transfers',{'a.txt':b'needle\n','b.txt':b'other\n','c.txt':b'third\n'})
  a.check('Tree A is Avail', 'Avail' in a.screen.text());a.send('a');a.check('Capacity displayed', 'Capacity' in a.screen.text());a.send('\x1b');a.send('\r')
  a.send('c');a.wait('COPY');a.send('\r');a.wait('To:');dest=a.dir/'dest';dest.mkdir();a.send('\x15'+str(dest)+'\r');a.wait('Replace existing');a.send('\r');a.wait('Copy 1');a.send('n');a.check('Copy cancelled leaves destination empty',not list(dest.iterdir()))
  a.send('c');a.send('\r');a.send('\x15'+str(dest)+'\r');a.send('\r');a.wait('Copy 1');a.send('y',.5);a.check('Single copy writes correct bytes',(dest/'a.txt').read_bytes()==b'needle\n')
  a.send('\x14');a.send(F4);a.send('c');a.wait('COPY 3');a.send('\r');a.send('\x15'+str(dest)+'\r');a.send(DOWN);a.send('\r');a.send('y');a.wait('Confirm copy/move for each');a.send('n',.6);a.check('Ctrl+C from menu copies tagged files',all((dest/p).exists() for p in ['a.txt','b.txt','c.txt']))
  a.send('r');a.wait('RENAME');a.send('\x15renamed.txt\r');a.send('y',.4);a.check('Rename keeps data',(a.root/'renamed.txt').read_bytes()==b'needle\n' and not(a.root/'a.txt').exists())
  a.send('m');a.wait('MOVE');a.send('\r');a.send('\x15'+str(dest)+'\r');a.send('\r');a.send('y',.5);a.check('Move removes source and writes destination',not(a.root/'renamed.txt').exists() and(dest/'renamed.txt').exists())
  a.send('d');a.wait('Delete 1');a.send('n');a.check('Delete N cancels',(a.root/'b.txt').exists());a.send('d');a.send('y',.4);a.check('Delete selected file',not(a.root/'c.txt').exists());a.check('Selection remains at nearby file',(a.root/'b.txt').exists())
  a.send('\x14');a.send('\x04');a.wait('Delete 1');a.send('y');a.wait('Confirm delete for each');a.send('n',.5);a.check('Ctrl+D initial and each-file confirmations remove remaining files',not list(a.root.iterdir()));a.send('\r');a.check('Empty file view can return to tree','TREE' in a.screen.text())
  a.send('m');a.wait('MAKE');a.send('new/sub\r',.5);a.check('M creates nested directories',(a.root/'new/sub').is_dir());a.send('d');a.send('y',.5);a.check('D deletes empty directory',not(a.root/'new/sub').exists());a.close()
  a=App('search-viewer',{'a.txt':b'alpha needle\nnext\nneedle twice\n','b.txt':b'no match\n'})
  a.send('\r');a.send('\x14');a.send('\x13');a.wait('SEARCH for:');a.send('needle\r',.6);a.check('Batch search keeps matching tags only','1 hits; 0 errors' in a.screen.text() and a.screen.text().count('♦')==1);a.send(HOME);a.send('v',.5);a.check('Viewer inherits search query','Match at' in a.screen.text());a.send(' ');a.check('Space navigates repeated match','Match at 18' in a.screen.text())
  for key,label in [('w','Wrap'),('r','Ruler'),('c','Charset'),('d','Dump'),('a','Alpha'),('j','Junk'),('h','Hex')]:a.send(key);a.check('Viewer '+label+' remains responsive',a.proc.poll() is None)
  a.send('h');a.send(F10);a.send('1');a.check('Viewer Ctrl bookmark via menu','Bookmark set' in a.screen.text());a.send(F10);a.send(F10);a.send('1');a.check('Viewer Alt bookmark via menu',a.proc.poll() is None)
  a.send('f');a.wait('SEARCH');a.send(UP);a.wait('SEARCH HISTORY');a.send('\r');a.check('History retrieves previous search','needle' in a.screen.text());a.send('\r');a.send('\x1b');a.send('f');a.send('\x15*.txt\r');a.send('f');a.send(UP);a.wait('FILESPEC HISTORY');a.send('\r');a.send('\r');a.check('Filespec history stored on disk',(a.home/'config/ltreec/filespec-history.json').exists());a.check('Search history uses shared config format',json.loads((a.home/'config/ltreec/search-history.json').read_text())['search'][0]['text']=='needle')
  a.send('\x1bi');a.check('Alt+I is a small overlay preserving file list','FILE INFORMATION' in a.screen.text() and 'DIR:' in a.screen.text());a.send('\x1bi');a.send('\r');a.send('?');a.wait('DISK STATISTICS');a.check('Statistics contain RAM and logged file counts','RAM total' in a.screen.text() and 'Total files' in a.screen.text());a.send('\x1b');a.close()
  a=App('archives',{'a.txt':b'archive test\n','b.txt':b'two\n'})
  a.send('\r');a.send('\x14');a.send(b'\x1b[15;5~');a.wait('CREATE archive');target=a.root/'demo.zip';a.send('\x15'+str(target));a.send(F3);a.wait('Archive format');a.check('Format chooser contains nine formats',all(f in a.screen.text() for f in ['ZIP','7Z','TAR.GZ','TAR.BZ2','XZ']));a.send('\r');a.send('\r',.7);a.check('Tagged ZIP created',target.exists());a.check('ZIP contains selected files',set(zipfile.ZipFile(target).namelist())=={'a.txt','b.txt'});
  a.send(END);a.send('\r',.5);a.wait('demo.zip /');a.check('Enter browses archive without Alt+F5', 'Extract' in a.screen.text());a.send('v',.3);a.check('Archive member viewer displays content','archive test' in a.screen.text());a.send('\x1b');a.send('\x14');a.send('\x03');a.wait('EXTRACT');dest=a.dir/'extracted';a.send('\x15'+str(dest)+'\r',.6);a.check('Archive extraction creates correct files',(dest/'a.txt').read_bytes()==b'archive test\n' and (dest/'b.txt').exists());a.send('\x1b');a.close()
  a=App('tree-safety',{'branch/sub/a.txt':b'one','branch/sub/b.txt':b'two','keep.txt':b'keep'})
  a.send('*',.5);a.send(DOWN);a.send('b');a.send('\x14');a.send('\x04');a.wait('Delete 2');a.send('y');a.send('n',.5);a.wait('Delete empty branch directories and parent');a.check('Branch parent waits for separate confirmation',(a.root/'branch').is_dir());a.send('y',.5);a.check('Branch parent removed only after confirmation',not(a.root/'branch').exists() and (a.root/'keep.txt').exists());
  a.send('m');a.send('prune/sub\r',.5);(a.root/'prune/sub/new.txt').write_text('safe');a.send(F3,.5);a.send('\x7f');a.send('\x1bp');a.wait('PRUNE');a.send('wrong\r');a.check('Prune refuses wrong keyword',(a.root/'prune/sub/new.txt').exists());a.send('\x1bp');a.send('PRUNE\r',.6);a.check('Prune removes logged branch safely',not(a.root/'prune').exists());a.close()
  a=App('shell-compare',{'a.txt':b'first\n','b.txt':b'second\n'})
  a.send('x');a.wait('EXECUTE');a.send('printf done > shell-result\r',.6);a.check('X command runs in selected directory',(a.root/'shell-result').read_text()=='done');a.check('X refreshes panel','shell-result' in a.screen.text());a.send('\r');a.send('j');a.wait('COMPARE file');a.send('\x15'+str(a.root/'b.txt')+'\r',.5);a.check('JFC displays comparison and changes','different bytes' in a.screen.text());a.send('h');a.check('Hex differences are navigable',a.proc.poll() is None);a.send('\x1b');a.send('\x1bs');a.wait('SORT');a.send(DOWN);a.send('\r');a.check('Alt+S sort chooser works',a.proc.poll() is None);a.send(F1);a.wait('LTree Commander Help');a.check('Full contextual help available','Terminal workspace' in a.screen.text());a.send('\x1b');a.send('q');a.send('y');a.check('Clean exit',a.proc.wait(timeout=5)==0);a.close();a=None
 finally:
  if a:a.close()
  (work/'pty-parity-checks.json').write_text(json.dumps(checks,indent=2))
 print(json.dumps(checks,indent=2))

if __name__=="__main__":main()
