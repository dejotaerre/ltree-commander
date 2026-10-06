import runpy,json,os,zipfile,time,signal,fcntl,termios,struct
from pathlib import Path
h=runpy.run_path(str(Path(__file__).with_name('terminal_pty.py')),run_name='harness');App=h['App'];F1=h['F1'];F3=h['F3'];F4=h['F4'];F5=h['F5'];F8=h['F8'];F10=h['F10'];UP=h['UP'];DOWN=h['DOWN'];END=h['END'];HOME=h['HOME'];LEFT=h['LEFT'];RIGHT=h['RIGHT'];work=h['work']
checks=h['checks'];a=None
try:
 a=App('split-global',{'a.txt':b'alpha','b.txt':b'bravo'})
 dest=a.dir/'dest';dest.mkdir();(dest/'existing.txt').write_text('existing')
 a.send('\r');a.send('\x14');a.send(F8);a.send('\t');a.send('l');a.send('p');a.send(str(dest)+'\r',.4);a.send('\r');a.send('\t');a.send('c');a.send('\r');a.send('\r');a.send('\r');a.send('y',.5)
 a.check('Opposite pane refreshed immediately after copy',(dest/'a.txt').exists() and a.screen.text().count('a')>0 and 'existing' in a.screen.text());a.check('Destination selection preserved','2/2' in a.screen.text())
 a.send('\r');a.send('g');a.check('Global includes both registered roots','GLOBAL' in a.screen.text() and 'Files 4' in a.screen.text());a.send('\x14');a.send('\x1b');a.check('Global tags published back to original session','Tagged 2' in a.screen.text());a.send('\t');a.check('Global tags published to other pane','Tagged 2' in a.screen.text());a.send('\t')
 a.send('h');a.wait('SYMLINK');link=a.dir/'link';a.send('\x15'+str(link)+'\r',.5);a.check('H creates directory symlink',link.is_symlink() and link.resolve()==a.root)
 a.send('>');a.check('Cycling logged roots restores saved location','split-global/dest' in a.screen.text());a.send('<');a.check('Cycling back restores original root','split-global/root' in a.screen.text());a.close()
 a=App('regex-history',{'a.txt':b'ORDER_123\nother\nORDER_456\n','b.txt':b'not order\n','c.txt':b'\xff\xfeA\x00\n\x00'})
 a.send('\r');a.send('\x14');a.send('\x13');a.send(F4);a.send(F4);a.send(F4);a.send('^ORDER_[0-9]+$\r',.6);a.check('Regex keeps matching files only',a.screen.text().count('♦')==1 and '1 hits' in a.screen.text());a.send(HOME);a.send('v',.4);a.send(' ');a.check('Inherited regex Space finds next line','Match at 16' in a.screen.text());a.send('f');a.send('\x15[\r');a.check('Invalid regex reports error without mutation','Invalid regex' in a.screen.text());a.check('Original text remains intact',(a.root/'a.txt').read_bytes().startswith(b'ORDER_123'))
 a.send('\x1b');a.send('f');a.send('\x15*.txt\r');a.send('f');a.send(UP);a.send(b'\x1b[2~');a.send(b'\x1b[3~');a.check('Marked history cannot be silently deleted','Unmark' in a.screen.text());a.send('\x1b');a.send('\x1b');a.close()
 a=App('viewer-tools',{'a.txt':b'first\nsecond\nthird\n','b.bin':b'\x00\x01\x02\x03'})
 a.send('\r');a.send('v',.4);a.send('g');a.send(DOWN);a.send('g');a.wait('GATHER');out=a.dir/'gather.txt';a.send('\r');a.send(str(out)+'\r',.4);a.check('Gather writes chosen text selection',out.exists() and out.read_text()=='first\nsecond\n')
 a.send('o');a.send('2\r');a.check('Offset prompt jumps to a line','ALPHA' in a.screen.text());a.send(F10);a.send('1');a.check('Viewer bookmark set through F10','Bookmark set' in a.screen.text());a.send(F10);a.send(F10);a.send(F5);a.check('Alt viewer menu permits color cycling',a.proc.poll() is None)
 a.send('\x1b');a.send(HOME);a.send(F3,.5);a.send('v',.3);a.send(b'\x1b[1;5R');a.check('Ctrl+F3 starts continuous reload','Continuous reload on' in a.screen.text());(a.root/'a.txt').write_text('refreshed\n');a.read(1.4);a.check('Continuous reload reflects external update','refreshed' in a.screen.text());a.send(b'\x1b[1;5R');a.check('Continuous reload remains enabled until toggled off','Continuous reload off' in a.screen.text());a.send('\x1b')
 a.send(HOME);a.send(b'\x1b[18~',.5);a.send('W');a.check('Shift+W operates Autoview without replacing list','AUTOVIEW' in a.screen.text() and 'DIR:' in a.screen.text());a.send('C');a.send('H');a.check('Autoview charset and Hex commands preserve file list','AUTOVIEW' in a.screen.text() and 'HEX' in a.screen.text());a.send('\x1b')
 a.send(END);a.send('e',.4);a.send('F');a.send('\r');a.wait('Save hex');a.send('n');a.check('N at Hex save prompt keeps unsaved edits','Modified' in a.screen.text() and (a.root/'b.bin').read_bytes()[0]==0);a.send('\r');a.send('y',.5);a.check('Hex confirmed byte replacement saved',(a.root/'b.bin').read_bytes()[0]==0xf0);a.send('\x1b');a.close()
 a=App('nested-archive',{'a.txt':b'text'})
 z=a.root/'nested.zip'
 with zipfile.ZipFile(z,'w') as archive:archive.writestr('dir/sub/file.txt','needle\n')
 a.send(F3,.4);a.send('\r');a.send(END);a.send('\r',.4);a.check('ZIP without explicit directory entries exposes synthesized folder','dir/' in a.screen.text());a.send('\r');a.check('ZIP directory navigation shows subfolder','sub/' in a.screen.text());a.send('b');a.send('\x14');a.send('\x13');a.send('needle\r',.4);a.check('Archive member content search reports hits','1 hits' in a.screen.text());a.send('\x1bc');a.wait('EXTRACT');dest=a.dir/'extracted';a.send('\x15'+str(dest)+'\r',.5);a.check('Alt+C extracts tagged archive members preserving paths',(dest/'dir/sub/file.txt').read_text()=='needle\n');a.close();a=None
finally:
 if a:a.close()
 (work/'pty-extended-checks.json').write_text(json.dumps(checks,indent=2))
print(json.dumps(checks,indent=2))
