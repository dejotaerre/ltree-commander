import runpy,json,os,zipfile,gzip,bz2,lzma,time
from pathlib import Path
h=runpy.run_path(str(Path(__file__).with_name('terminal_pty.py')),run_name='harness');App=h['App'];F1=h['F1'];F3=h['F3'];F4=h['F4'];F5=h['F5'];F10=h['F10'];UP=h['UP'];DOWN=h['DOWN'];END=h['END'];HOME=h['HOME'];work=h['work'];checks=h['checks'];a=None
try:
 a=App('formats',{'source.txt':b'format payload\n'})
 a.send('\r')
 for extension in ['zip','tar','tar.gz','tar.bz2','tar.xz','7z','gz','bz2','xz']:
  target=a.dir/('sample.'+extension);a.send(F5);a.wait('CREATE');a.send('\x15'+str(target)+'\r',.5);a.check('Create '+extension,target.exists() and target.stat().st_size>0)
  if extension in ['gz','bz2','xz']:
   module={'gz':gzip,'bz2':bz2,'xz':lzma}[extension];a.check('Decode stream '+extension,module.open(target,'rb').read()==b'format payload\n')
 a.close()
 a=App('graft-compare',{'branch/a.txt':b'one','same.txt':b'equal','unique.txt':b'new'})
 dest=a.dir/'dest';dest.mkdir();(dest/'same.txt').write_text('equal')
 a.send('*',.4);a.send('c');a.send('\x15'+str(dest)+'\r');a.send(DOWN);a.send(DOWN);a.send(DOWN);a.send('\r');a.send('\r',.5);a.check('Directory compare unique selects missing file','1 matches; 0 errors' in a.screen.text())
 a.send(DOWN);a.send('\x1bg');a.wait('GRAFT');a.send('\x15'+str(dest)+'\r');a.wait('Move branch');a.check('Graft waits for confirmation',(a.root/'branch/a.txt').exists());a.send('y',.5);a.check('Graft moves branch and contents',not(a.root/'branch').exists() and (dest/'branch/a.txt').read_bytes()==b'one');a.close()
 a=App('error-cancel',{'a.txt':b'one','b.txt':b'two'})
 a.send('\r');a.send('\x14');(a.root/'a.txt').unlink();a.send('\x13');a.send('needle\r');a.wait('Keep tag');a.send(END);a.send('\r');a.check('Search cancel after read error preserves unprocessed tags',a.screen.text().count('♦')==2 and 'cancelled' in a.screen.text());a.close()
 a=App('viewer-fidelity',{'a.txt':b'first\nsecond\nthird\n','b.bin':b'\x00ABC\x01DE\nF','c.txt':b'\xff\xfe'+ 'first\r\nsecond'.encode('utf-16le')})
 a.send('\r');a.send('a');a.send(F1);a.wait('LTree Commander Help');a.check('Permissions prompt has contextual help','Permissions' in a.screen.text());a.send('\x1b');a.send('\x1b')
 a.send(DOWN);a.send('v',.4);a.send('j');a.check('Junk joins printable ASCII bytes','ABCDEF' in a.screen.text());a.send('h');a.send('f');a.send('DE\r');a.check('Text search in Hex uses byte offsets','Match at 5' in a.screen.text());a.send(F10);a.send(F10);a.send('p');a.wait('PRINT');out=a.dir/'hex.txt';a.send(str(out)+'\r');a.check('Hex Print writes readable dump','00 41 42 43 01 44 45 0A 46' in out.read_text());a.send('\x1b')
 a.send(END);a.send('v',.4);a.send('h');a.send('f');a.send(F4);a.send(F4);a.send('\x15second\r');a.check('Unicode search in Hex uses UTF16 byte offsets','Match at 16' in a.screen.text());a.send('\x1b');a.close();a=None
finally:
 if a:a.close()
 (work/'extra-checks.json').write_text(json.dumps(checks,indent=2))
print(json.dumps(checks,indent=2))
