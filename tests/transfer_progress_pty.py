import runpy,os,time,json,re,tempfile,fcntl,termios,struct
from pathlib import Path
tests=Path(__file__).resolve().parent
h=runpy.run_path(str(tests/'terminal_pty.py'),run_name='transfer_harness')
base=h['work']
App=h['App'];checks=h['checks']
for move in [False,True]:
 for cancel in [False,True]:
  a=App(('move' if move else 'copy')+('-cancel' if cancel else '-complete'),{'large.bin':b'x'*(8*1024*1024)},extra_env={'LD_PRELOAD':os.environ['LTC_TRANSFER_TEST_DELAY_LIBRARY']})
  with tempfile.TemporaryDirectory(prefix='ltc-progress-',dir='/dev/shm' if move else str(a.dir)) as temp:
   try:
    fcntl.ioctl(a.master,termios.TIOCSWINSZ,struct.pack('HHHH',32,80,0,0));a.screen=h['Screen'](32,80);a.read(.2)
    dest=Path(temp);target=dest/'large.bin'
    a.send('\r');a.send('m' if move else 'c');a.send('\r');a.send('\x15'+str(dest)+'\r');a.send('\r');a.wait('Move 1' if move else 'Copy 1');a.send('y',.01)
    observed=[];deadline=time.monotonic()+4
    while time.monotonic()<deadline:
     screen=a.read(.08)
     if re.search(r'\b[1-9][0-9]?%',screen) and 'MiB/s' in screen:
      observed.append(screen)
     if observed and 'Syncing' in screen:break
    a.check('Terminal transfer shows changing bytes percentage speed and estimated time',len(observed)>1 and any(' L:' in x for x in observed))
    a.check('Spinner continues during transfer',len({x.splitlines()[-1][0] for x in observed})>1)
    (base/('terminal-'+('move' if move else 'copy')+'-progress-80.txt')).write_text(observed[0])
    a.check('Synchronization stays below completion','Syncing' in a.screen.text() and '99%' in a.screen.text() and not target.exists())
    (base/('terminal-'+('move' if move else 'copy')+'-syncing-80.txt')).write_text(a.screen.text())
    if cancel:a.send('\x1b',.01)
    deadline=time.monotonic()+6
    while time.monotonic()<deadline:
     a.read(.1)
     if 'Transfer cancelled' in a.screen.text() or re.search(r'(Move|Copy): 1 completed',a.screen.text()):break
    a.check('Terminal cancellation/completion preserves correct files',((a.root/'large.bin').exists() and not target.exists()) if cancel else (target.read_bytes()==b'x'*(8*1024*1024) and (a.root/'large.bin').exists()!=move))
    a.check('Transfer temporary cleaned',not list(dest.glob('.ltree-copy-*')))
   finally:a.close()
(base/(Path(os.environ['LTC_TEST_BINARY']).name+'-transfer-checks.json')).write_text(json.dumps(checks,indent=2))
print(json.dumps(checks,indent=2))
