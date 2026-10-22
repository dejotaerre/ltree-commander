import runpy,os,re,zlib,struct,binascii,json,shlex
from pathlib import Path
h=runpy.run_path(str(Path(__file__).with_name('terminal_pty.py')),run_name='harness')
App=h['App'];checks=h['checks'];work=h['work'];apps=[]

def png():
 def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',binascii.crc32(kind+data)&0xffffffff)
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',32,16,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\x00'+b'\xff\x00\x00'*32)*16))+chunk(b'IEND',b'')

helpers=work/'forbidden-visual-helpers';helpers.mkdir(exist_ok=True);called=work/'visual-helper-called'
for name in ['ffmpeg','ffprobe','magick','convert','pdftoppm','pdfinfo','libreoffice','soffice']:
 p=helpers/name;p.write_text('#!/bin/sh\nprintf called >> '+shlex.quote(str(called))+'\nexit 99\n');p.chmod(0o755)
fixtures={'png':png(),'pdf':b'%PDF-1.4\n'+b'\0'*64,'docx':b'PK\x03\x04'+b'\0'*64,'ttf':b'\0\x01\0\0'+b'\0'*64,'pf2':b'FILE\0\0\0\x04PFF2'+b'\0'*64,'mkv':b'\x1aE\xdf\xa3'+b'\0'*64}
try:
 for extension,data in fixtures.items():
  a=App('raw-'+extension,{'sample.'+extension:data},extra_env={'TERM':'xterm-kitty','PATH':str(helpers)+':/usr/bin:/bin','TMUX':'','STY':''});apps.append(a)
  a.send('\r');a.wait('DIR:');a.send('\x1b[18~');a.wait('AUTOVIEW HEX:')
  signature=' '.join(f'{byte:02X}' for byte in data[:4]);a.check(extension+' preview shows original hexadecimal bytes',signature in a.screen.text())
  a.send('A');a.wait('AUTOVIEW TEXT:');a.send('H');a.wait('AUTOVIEW HEX:')
  a.check(extension+' preview supports raw Alpha/Hex switching',b'\x1b_G' not in a.transcript and 'Visual preview' not in a.screen.text())
  a.send('\x1b');a.send('v');a.wait('VIEW:');a.check(extension+' V viewer remains raw',signature in a.screen.text())
 a=App('plain-raw',{'sample.png':png()});apps.append(a);a.send('\r');a.wait('DIR:');a.send('\x1b[18~');a.wait('AUTOVIEW HEX:');a.check('Ordinary terminals use the same raw preview','89 50 4E 47' in a.screen.text())
 text=App('raw-text',{'sample.txt':b'Plain text stays plain\nsecond line\n'});apps.append(text);text.send('\r');text.wait('DIR:');text.send('\x1b[18~');text.wait('AUTOVIEW TEXT:');text.check('Text Autoview stays readable','Plain text stays plain' in text.screen.text())
 text.send('H');text.wait('AUTOVIEW HEX:');text.send('A');text.wait('AUTOVIEW TEXT:')
 text.check('No visual helper was launched for any file type',not called.exists())
 a=App('tree-default',{'file.txt':b'x'*2048});apps.append(a);a.wait('Files 1')
 a.check('Tree sizes visible without an option',bool(re.search(r'[\[{]\s*[\d.]+k[\]}]',a.screen.text())))
 a.check('Terminal sidebar displays release and build','LTC 0.1.0-alpha.1' in a.screen.text() and bool(re.search(r'Build [0-9a-f]{10}',a.screen.text())))
 b=App('tree-hidden',{'file.txt':b'x'*2048},terminal_args=['--terminal','--no-tree-sizes']);apps.append(b);b.wait('Files 1')
 b.check('No-tree-sizes explicitly hides annotations',not re.search(r'[\[{]\s*[\d.]+k[\]}]',b.screen.text()))
finally:
 for app in apps:app.close()
 (work/'checks.json').write_text(json.dumps(checks,indent=2))
print(json.dumps(checks))
