import runpy,os,time,json,re,tempfile,fcntl,termios,struct
from pathlib import Path
h=runpy.run_path(str(Path(__file__).with_name('terminal_pty.py')),run_name='graft_harness')
App=h['App'];checks=h['checks'];work=h['work']
payload=b'g'*(8*1024*1024)
for columns in [80,140]:
 for cancel in [False,True]:
  a=App('graft-'+str(columns)+('-cancel' if cancel else '-complete'),{'branch/large.bin':payload},
        startup_directory='branch',extra_env={'LD_PRELOAD':os.environ['LTC_GRAFT_TEST_DELAY_LIBRARY']})
  with tempfile.TemporaryDirectory(prefix='ltc-graft-progress-',dir='/dev/shm') as directory:
   try:
    fcntl.ioctl(a.master,termios.TIOCSWINSZ,struct.pack('HHHH',32,columns,0,0));a.screen=h['Screen'](32,columns);a.read(.2)
    source=a.root/'branch';target=Path(directory)/'branch'/'large.bin'
    a.send('\x1bg');a.wait('GRAFT branch to:');a.send('\x15'+directory+'\r');a.wait('Move branch');a.send('y',.01)
    frames=[];deadline=time.monotonic()+5
    while time.monotonic()<deadline:
     screen=a.read(.08)
     if re.search(r'\b[1-9][0-9]?%',screen) and 'MiB/s' in screen and 'large.bin' in screen:frames.append(screen)
     if len(frames)>=4:break
    a.check('Graft '+str(columns)+' shows current file bytes percentage speed and ETA',len(frames)>=4 and any(' L:' in frame or 'Left:' in frame for frame in frames))
    a.check('Graft spinner keeps moving',len({frame.splitlines()[-1][0] for frame in frames})>1)
    (work/('graft-progress-'+str(columns)+'.txt')).write_text(frames[-1])
    a.check('Graft remains below 100 until completion',all('100%' not in frame for frame in frames))
    if cancel:a.send('\x1b',.01)
    a.wait('Graft cancelled' if cancel else 'Branch moved',10)
    a.check('Graft cancellation preserves complete source' if cancel else 'Graft completes correctly',
            (source/'large.bin').read_bytes()==payload if cancel else target.read_bytes()==payload and not source.exists())
    a.check('Graft restores staged source name after cancellation',not list(a.root.glob('.ltree-graft-*')))
   finally:a.close()
(work/'graft-checks.json').write_text(json.dumps(checks,indent=2))
print(json.dumps(checks,indent=2))
