#include "fs/fontpreview.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_FONT_FORMATS_H
#include <algorithm>
#include <cstdint>
#include <vector>

namespace ltree {
struct FontPreviewData {
    Bytes bytes;
    std::vector<uint32> characters,offsets;
    bool pf2=false;
    int height=16;
};
namespace {
constexpr int canvasWidth=1000,canvasHeight=680,perPage=96;
uint32 be32(const Bytes &data,size_t at){return fromBigEndian<uint32>(reinterpret_cast<const unsigned char*>(data.constData()+at));}
uint16 be16(const Bytes &data,size_t at){return fromBigEndian<uint16>(reinterpret_cast<const unsigned char*>(data.constData()+at));}
bool visible(uint32 c){return c>=32 && !(c>=0x7f && c<0xa0) && !(c>=0xd800 && c<=0xdfff) && c<=0x10ffff;}
struct Face {
    FT_Library library=nullptr;FT_Face face=nullptr;
    explicit Face(const Bytes &data){if(!FT_Init_FreeType(&library))FT_New_Memory_Face(library,reinterpret_cast<const FT_Byte*>(data.constData()),data.size(),0,&face);}
    ~Face(){if(face)FT_Done_Face(face);if(library)FT_Done_FreeType(library);}
};
struct Glyph {int width=0,height=0,left=0,top=0,advance=0;std::vector<unsigned char> pixels;};
Glyph glyph(const FontPreviewData &data,FT_Face face,uint32 code,int size)
{
    Glyph result;
    if(data.pf2){
        const auto found=std::lower_bound(data.characters.begin(),data.characters.end(),code);
        if(found==data.characters.end() || *found!=code){result.advance=size/2;return result;}
        const size_t at=data.offsets[found-data.characters.begin()];const int scale=std::max(1,size/data.height);
        const int w=be16(data.bytes,at),h=be16(data.bytes,at+2);
        result.width=w*scale;result.height=h*scale;result.left=int(int16_t(be16(data.bytes,at+4)))*scale;
        result.top=(h+int(int16_t(be16(data.bytes,at+6))))*scale;result.advance=int(int16_t(be16(data.bytes,at+8)))*scale;
        if(result.width>2048 || result.height>2048)return {};
        result.pixels.resize(result.width*result.height);
        for(int y=0;y<result.height;++y)for(int x=0;x<result.width;++x){
            const size_t bit=(y/scale)*w+x/scale;
            result.pixels[y*result.width+x]=(static_cast<unsigned char>(data.bytes[at+10+bit/8])&(0x80>>(bit%8)))?255:0;
        }
    }else{
        if(!face)return result;
        if(FT_Set_Pixel_Sizes(face,0,size) && face->num_fixed_sizes){
            int nearest=0;for(int i=1;i<face->num_fixed_sizes;++i)if(std::abs(face->available_sizes[i].height-size)<std::abs(face->available_sizes[nearest].height-size))nearest=i;
            FT_Select_Size(face,nearest);
        }
        if(FT_Load_Char(face,code,FT_LOAD_RENDER))return result;
        const auto &g=*face->glyph;const auto &bitmap=g.bitmap;
        result.width=bitmap.width;result.height=bitmap.rows;result.left=g.bitmap_left;result.top=g.bitmap_top;result.advance=g.advance.x/64;
        if(result.width>2048 || result.height>2048)return {};
        result.pixels.resize(result.width*result.height);
        for(int y=0;y<result.height;++y){
            const auto *row=bitmap.buffer+(bitmap.pitch>=0?y:result.height-1-y)*std::abs(bitmap.pitch);
            for(int x=0;x<result.width;++x){
                auto &pixel=result.pixels[y*result.width+x];
                if(bitmap.pixel_mode==FT_PIXEL_MODE_MONO)pixel=(row[x/8]&(0x80>>(x%8)))?255:0;
                else if(bitmap.pixel_mode==FT_PIXEL_MODE_GRAY)pixel=bitmap.num_grays>1?row[x]*255/(bitmap.num_grays-1):0;
                else if(bitmap.pixel_mode==FT_PIXEL_MODE_BGRA)pixel=row[x*4+3];
            }
        }
    }
    return result;
}
void pixel(PreviewRaster &out,int x,int y,int alpha,bool cyan=false)
{
    if(x<0 || y<0 || x>=out.width || y>=out.height)return;
    const int at=(y*out.width+x)*3;
    const int color[3]={cyan?0:255,255,cyan?255:0};
    for(int c=0;c<3;++c)out.rgb[at+c]=char((color[c]*alpha+(c==2?128:0)*(255-alpha))/255);
}
void draw(PreviewRaster &out,const Glyph &g,int x,int baseline)
{
    for(int y=0;y<g.height;++y)for(int col=0;col<g.width;++col)if(g.pixels[y*g.width+col])pixel(out,x+g.left+col,baseline-g.top+y,g.pixels[y*g.width+col]);
}
void line(PreviewRaster &out,const FontPreviewData &data,FT_Face face,const char32_t *text,int x,int baseline,int size)
{
    for(auto p=text;*p && x<out.width-25;++p){const auto g=glyph(data,face,*p,size);draw(out,g,x,baseline);x+=std::max(1,g.advance);}
}
void label(PreviewRaster &out,const String &text,int x,int y)
{
    // Etiquetas independientes de la fuente seleccionada, incluso para tipografías de símbolos.
    constexpr unsigned short shapes[]={0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf,0x2bed,0x6bae,0x7927,0x6b6e,0x79e7,0x79e4};
    for(const auto c:text.toUtf8()){
        const int n=c>='0' && c<='9'?c-'0':c>='A' && c<='F'?c-'A'+10:-1;
        if(n>=0)for(int row=0;row<5;++row)for(int col=0;col<3;++col)if(shapes[n]&(1<<(14-row*3-col)))
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)pixel(out,x+col*2+dx,y+row*2+dy,255,true);
        x+=8;
    }
}
bool pf2(PreviewSource &source,FontPreviewData &data,const Cancellation &cancel)
{
    const auto &bytes=data.bytes;size_t at=0,index=0,indexSize=0,bitmap=0;String name="PFF2";
    if(bytes.size()<12 || bytes.left(4)!="FILE" || be32(bytes,4)!=4 || bytes.mid(8,4)!="PFF2")return false;
    while(at+8<=size_t(bytes.size())){
        if(cancel->load())return false;
        const auto tag=bytes.mid(at,4);const size_t length=be32(bytes,at+4);at+=8;
        if(tag=="DATA"){if(length!=0xffffffff && length>size_t(bytes.size())-at)return false;bitmap=at;break;}
        if(length>size_t(bytes.size())-at)return false;
        if(tag=="NAME")name=String::fromUtf8(bytes.mid(at,std::min(length,size_t(256))).split('\0').front());
        if(tag=="MAXH" && length==2)data.height=std::max(1,int(be16(bytes,at)));
        if(tag=="CHIX"){index=at;indexSize=length;}
        at+=length;
    }
    if(!bitmap || !index || indexSize%9 || indexSize/9>0x110000 || data.height>2048)return false;
    uint32 previous=0;
    for(size_t i=0;i<indexSize;i+=9){
        if(cancel->load())return false;
        const size_t item=index+i;const auto code=be32(bytes,item);const size_t offset=be32(bytes,item+5);
        if((i && code<=previous) || (static_cast<unsigned char>(bytes[item+4])&7) || offset<bitmap || offset>size_t(bytes.size()) || size_t(bytes.size())-offset<10)return false;
        previous=code;const size_t w=be16(bytes,offset),h=be16(bytes,offset+2);
        if(w>2048 || h>2048 || (w*h+7)/8>size_t(bytes.size())-offset-10)return false;
        if(visible(code)){data.characters.push_back(code);data.offsets.push_back(offset);}
    }
    source.details=name+String(" | PFF2 bitmap | %1 chars").arg(data.characters.size());data.pf2=true;return !data.characters.empty();
}
}
void prepareFontPreview(PreviewSource &source,const Cancellation &cancel)
{
    auto data=std::make_shared<FontPreviewData>();File file(source.path);
    if(file.size()>64*1024*1024 || !file.open(IO::ReadOnly)){source.error="Cannot read font (maximum size: 64 MiB)";return;}
    while(!file.atEnd()){
        if(cancel->load())return;
        const auto bytes=file.read(65536);if(bytes.isEmpty()){source.error="Cannot read font data";return;}
        data->bytes+=bytes;if(data->bytes.size()>64*1024*1024){source.error="Font exceeds 64 MiB";return;}
    }
    if(FileInfo(source.path).suffix().toLower()=="pf2"){
        if(!pf2(source,*data,cancel)){if(!cancel->load())source.error="Invalid or unsupported PFF2 font (uncompressed glyphs required)";return;}
    }else{
        Face holder(data->bytes);const auto face=holder.face;
        if(!face){source.error="Cannot decode this font";return;}
        if(FT_Select_Charmap(face,FT_ENCODING_UNICODE)){source.error="Font has no Unicode character map";return;}
        FT_UInt glyphIndex=0;auto c=FT_Get_First_Char(face,&glyphIndex);
        while(glyphIndex){
            if(cancel->load())return;
            if(visible(c))data->characters.push_back(c);
            c=FT_Get_Next_Char(face,c,&glyphIndex);
            if(data->characters.size()>0x110000){source.error="Invalid font character map";return;}
        }
        if(data->characters.empty()){source.error="Font has no displayable characters";return;}
        source.details=String::fromUtf8(face->family_name?face->family_name:"Font")+" "+String::fromUtf8(face->style_name?face->style_name:"")+" | "+String::fromUtf8(FT_Get_Font_Format(face))+String(" | %1 chars").arg(data->characters.size());
        if(face->num_faces>1)source.details+=String(" | Face 1/%1").arg(face->num_faces);
    }
    source.pages=1+(data->characters.size()+perPage-1)/perPage;source.font=data;
}
PreviewRaster renderFontPreview(const std::shared_ptr<PreviewSource> &source,int page,const Cancellation &cancel)
{
    PreviewRaster out;out.source=source;out.page=page;
    if(!source->font){out.error="No font data available";return out;}
    const auto &data=*source->font;Face holder(data.pf2?Bytes():data.bytes);
    if(!data.pf2 && (!holder.face || FT_Select_Charmap(holder.face,FT_ENCODING_UNICODE))){out.error="Cannot open font for rendering";return out;}
    out.width=canvasWidth;out.height=canvasHeight;out.rgb.resize(canvasWidth*canvasHeight*3);
    for(Index i=0;i<out.rgb.size();++i)out.rgb[i]=char(i%3==2?128:0);
    if(page==1){
        if(std::binary_search(data.characters.begin(),data.characters.end(),uint32('A'))){
            line(out,data,holder.face,U"Aa Bb Cc 0123 ÁÉÍÓÚ Ññ",30,90,56);
            line(out,data,holder.face,U"The quick brown fox jumps over the lazy dog.",30,170,32);
            line(out,data,holder.face,U"Café, résumé, naïve, piñata — ÁÉÍÓÚ ü ñ.",30,225,32);
            line(out,data,holder.face,U"ABCDEFGHIJKLMNOPQRSTUVWXYZ",30,305,40);
            line(out,data,holder.face,U"abcdefghijklmnopqrstuvwxyz",30,365,40);
            line(out,data,holder.face,U"0123456789 !? @#$% &* () [] {}",30,425,32);
            int y=490;for(int size:{16,24,32,48}){label(out,String::number(size),30,y-12);line(out,data,holder.face,U"Abcde 0123456789",90,y,size);y+=size+13;}
        }else{
            for(size_t i=0;i<std::min(data.characters.size(),size_t(48));++i){if(cancel->load())break;const auto g=glyph(data,holder.face,data.characters[i],48);draw(out,g,30+(i%8)*120,85+(i/8)*105);}
        }
    }else{
        const size_t first=size_t(page-2)*perPage;
        for(size_t i=0;i<perPage && first+i<data.characters.size();++i){
            if(cancel->load())break;
            const int x=20+(i%12)*80,y=15+(i/12)*82;const auto code=data.characters[first+i];
            label(out,String("%1").arg(code,6,16,Char('0')).toUpper(),x+12,y+4);
            const auto g=glyph(data,holder.face,code,40);
            draw(out,g,x+40-g.width/2-g.left,y+54+g.top/2-g.height/2);
            for(int dx=0;dx<78;++dx)pixel(out,x+dx,y+80,80,true);
        }
    }
    if(cancel->load()){out.rgb.clear();out.cancelled=true;}return out;
}
}
