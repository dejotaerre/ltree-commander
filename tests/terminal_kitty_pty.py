import runpy,json,os,signal,time
from pathlib import Path
h=runpy.run_path(str(Path(__file__).with_name('terminal_pty.py')),run_name='harness')
App=h['App'];checks=h['checks'];work=h['work'];a=None

def key(code,mods=0,event=1,text=None,final='u'):
 return '\x1b['+str(code)+';'+str(mods+1)+':'+str(event)+(';' + ':'.join(map(str,text)) if text is not None else '')+final

def negotiate(app):
 app.check('Queries protocol without relying on TERM',b'\x1b[?u\x1b[c' in app.transcript)
 app.send('\x1b[?0u\x1b[?1;2c')
 app.check('Negotiates all-key events and text',b'\x1b[>27u' in app.transcript)

try:
 a=App('kitty-modifiers',{'a.txt':b'needle alpha\nneedle bravo\n','b.txt':b'other\n','c.txt':b'third\n'})
 negotiate(a)
 a.send(key(57442,4));a.check('Left Ctrl press displays CTRL menu','CTRL:' in a.screen.text())
 a.send(key(57442,0,3));a.check('Ctrl release restores normal menu','CTRL:' not in a.screen.text())
 a.send(key(57449,2));a.check('Right Alt press displays ALT menu','ALT:' in a.screen.text())
 a.send(key(57449,0,3));a.check('Alt release restores normal menu','ALT:' not in a.screen.text())
 a.send(key(57442,4)+key(57443,6));a.check('Ctrl takes precedence when both held','CTRL:' in a.screen.text())
 a.send(key(57442,2,3));a.check('Releasing Ctrl preserves held Alt','ALT:' in a.screen.text())
 a.send(key(57443,0,3)+key(13));a.wait('DIR:')
 a.send(key(ord('t'),4)+key(ord('t'),4,3));a.check('Ctrl+T tags every file','Tagged 3' in a.screen.text())
 a.send(key(ord('i'),4));a.wait('INVERT');a.send(key(13));a.check('Ctrl+I inverts tags instead of Tab','Tagged 0' in a.screen.text())
 a.send(key(ord('i'),0,3));a.check('Letter release never executes Invert','Tagged 0' in a.screen.text())
 a.send('\x1b[B');a.check('Abbreviated CSI arrow moves once','2/3' in a.screen.text());a.send('\x1b[H')
 a.send(key(1,0,1,final='B')+key(1,0,2,final='B'));a.check('Down press and repeat move twice','3/3' in a.screen.text())
 a.send(key(1,0,3,final='B'));a.check('Down release does not move','3/3' in a.screen.text())
 a.send(key(ord('t'),4)+key(ord('m'),4));a.wait('MOVE 3');a.check('Ctrl+M starts tagged Move instead of Enter','DIR:' in a.screen.text());a.send(key(27))
 a.send(key(ord('j'),4));a.check('Ctrl+J reaches tagged comparison','COMPARE file with:' in a.screen.text());a.send(key(27))
 a.send(key(ord('i'),2));a.check('Alt+I preserves file list with info overlay','FILE INFORMATION' in a.screen.text() and 'DIR:' in a.screen.text())
 a.send(key(ord('i'),0,3));a.check('Alt+I release leaves overlay open','FILE INFORMATION' in a.screen.text());a.send(key(ord('i'),2))
 a.send(key(21,0,final='~'));a.check('Encoded F10 does not leak sequence into commands',a.proc.poll() is None)
 a.send(key(ord('f')));a.wait('FILESPEC:')
 a.send(key(ord('u'),4)+key(0,0,text=[42,46,116,120,116])+key(13));a.check('IME associated text can set Filespec','*.txt' in a.screen.text())
 a.send(key(ord('f')));a.send(key(ord('u'),4));a.send('\x1b[108;1:',.08);a.send('1;108u'+key(127)+key(0,0,text=[42,46,116,120,116])+key(13));a.check('Fragmented CSI remains a single input','*.txt' in a.screen.text())
 a.send(key(ord('f')));a.send(key(ord('u'),4));a.send(key(0,0,text=[42,46,99,97,102,233]));a.check('Unicode text reaches prompt intact','*.café' in a.screen.text());a.send(key(27))
 a.send(key(1,0,final='H')+key(ord('v')),.4);a.wait('VIEW:')
 a.send(key(57443,2));a.check('Viewer displays held Alt menu','ALT VIEW:' in a.screen.text())
 a.send(key(57443,0,3));a.check('Viewer release restores normal menu','ALT VIEW:' not in a.screen.text())
 a.send(key(ord('1'),4));a.check('Ctrl+digit sets viewer bookmark','Bookmark set' in a.screen.text())
 a.send(key(27));a.send(key(ord('f')));a.send(key(ord('u'),4));a.send('\x1b[999999999999;1u'+key(ord('d'),0,3));a.check('Malformed and released keys cannot populate prompt','FILESPEC:' in a.screen.text() and '999999' not in a.screen.text());a.send(key(27))
 a.send(key(ord('x')));a.wait('EXECUTE');before=bytes(a.transcript)
 command='printf restored > kitty-shell-result'
 a.send(command+key(13),.6)
 a.check('Shell handoff pops and restores keyboard protocol',bytes(a.transcript)[len(before):].find(b'\x1b[<u')>=0 and bytes(a.transcript)[len(before):].find(b'\x1b[>27u')>=0)
 a.check('Shell command executes normally',(a.root/'kitty-shell-result').read_text()=='restored')
 a.send(key(1,0,final='H'))
 editor=a.home/'editor';editor.write_text('#!/bin/sh\nprintf edited >> "$1"\n');editor.chmod(0o700)
 (a.home/'.selected_editor').write_text('SELECTED_EDITOR='+str(editor)+'\n')
 before=bytes(a.transcript);a.send(key(ord('e')),.6)
 a.check('Editor receives normal mode and protocol resumes',b'\x1b[<u' in bytes(a.transcript)[len(before):] and b'\x1b[>27u' in bytes(a.transcript)[len(before):] and (a.root/'a.txt').read_text().endswith('edited'))
 a.send(key(ord('q')));a.send(key(ord('y')));a.proc.wait(timeout=5);a.read(.1)
 a.check('Normal exit pops protocol',a.transcript.rfind(b'\x1b[<u')>a.transcript.rfind(b'\x1b[>27u'));a.close();a=None
 a=App('kitty-signal',{'a.txt':b'safe'});negotiate(a);a.send(key(57442,4));os.kill(a.proc.pid,signal.SIGTERM);a.proc.wait(timeout=5);a.read(.1);a.check('Signal exit pops protocol',a.transcript.rfind(b'\x1b[<u')>a.transcript.rfind(b'\x1b[>27u'));a.close();a=None
finally:
 if a:a.close()
 (work/'pty-kitty-checks.json').write_text(json.dumps(checks,indent=2))
print(json.dumps(checks,indent=2))
