from pathlib import Path
import argparse
import gzip, struct

parser=argparse.ArgumentParser(description='Build the LTree console font from an installed PSF2 8x16 font.')
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args=parser.parse_args()
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

# Convierte la fuente Linux instalada a contornos que Qt puede cargar, sin cambiar sus píxeles.
data=args.source.read_bytes()
if args.source.suffix=='.gz':data=gzip.decompress(data)
magic,version,header,flags,count,size,height,width=struct.unpack_from('<8I',data)
assert magic==0x864ab572 and version==0 and flags&1 and (width,height,size)==(8,16,16)
names=['.notdef']+[f'g{i}' for i in range(count)]
glyphs={};cmap={};pos=header+count*size
pen=TTGlyphPen(None);glyphs['.notdef']=pen.glyph()
for i in range(count):
    pen=TTGlyphPen(None)
    pixels={(x,y) for y,bits in enumerate(data[header+i*size:header+(i+1)*size]) for x in range(8) if bits&(0x80>>x)}
    directions=[(1,0),(0,1),(-1,0),(0,-1)]
    edges=set()
    for x,y in pixels:
        if (x,y-1) not in pixels:edges.add((x,y,0))
        if (x+1,y) not in pixels:edges.add((x+1,y,1))
        if (x,y+1) not in pixels:edges.add((x+1,y+1,2))
        if (x-1,y) not in pixels:edges.add((x,y+1,3))
    # Une los bordes contiguos para evitar costuras entre los píxeles de un glifo.
    while edges:
        x,y,direction=next(iter(edges));start=(x,y)
        pen.moveTo((x*64,(13-y)*64))
        while True:
            edges.remove((x,y,direction));dx,dy=directions[direction];x+=dx;y+=dy
            if (x,y)==start:break
            pen.lineTo((x*64,(13-y)*64))
            direction=next(d for d in ((direction+1)%4,direction,(direction-1)%4,(direction+2)%4) if (x,y,d) in edges)
        pen.closePath()
    glyphs[f'g{i}']=pen.glyph()
    end=data.index(b'\xff',pos)
    mapping=data[pos:end].split(b'\xfe')[0].decode('utf-8')
    for char in mapping:cmap[ord(char)]=f'g{i}'
    pos=end+1
font=FontBuilder(1024,isTTF=True)
font.setupGlyphOrder(names);font.setupCharacterMap(cmap);font.setupGlyf(glyphs)
font.setupHorizontalMetrics({name:(512,getattr(glyphs[name],'xMin',0)) for name in names})
font.setupHorizontalHeader(ascent=832,descent=-192,lineGap=0)
font.setupNameTable({'familyName':'LTree Console','styleName':'Regular','uniqueFontIdentifier':'LTree Commander Console 8x16','fullName':'LTree Console Regular','psName':'LTreeConsole-Regular','version':'Version 1.0'})
font.setupOS2(version=4,sTypoAscender=832,sTypoDescender=-192,sTypoLineGap=0,usWinAscent=832,usWinDescent=192,fsSelection=0xC0)
font.setupPost(isFixedPitch=1);font.setupMaxp()
# Fija las fechas internas para obtener el mismo archivo a partir de la misma entrada.
font.font.recalcTimestamp=False
font.font['head'].created=font.font['head'].modified=2082844800
output=args.output
output.parent.mkdir(parents=True,exist_ok=True);font.save(output)
print(output)
