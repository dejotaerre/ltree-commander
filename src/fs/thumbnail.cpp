#include "fs/thumbnail.h"
#include <openssl/evp.h>
#include <png.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace ltree {
namespace {
struct CloseFile { void operator()(FILE *file) const { std::fclose(file); } };
void pngError(png_structp png,png_const_charp){png_longjmp(png,1);}
void pngWarning(png_structp,png_const_charp){}
String fileUri(const String &path)
{
    String uri="file://";
    for(unsigned char c:path.toUtf8()){
        // Coincide con el escape de rutas locales usado por GLib/Nautilus.
        if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || std::strchr("/-_.!~*'()@&=+$,:",c))uri+=Char(c);
        else uri+="%"+String("%1").arg(c,2,16,Char('0')).toUpper();
    }
    return uri;
}
bool load(const String &path,const String &uri,const struct stat &original,PreviewSource &source,const Cancellation &cancel)
{
    struct stat cached{};
    const auto filename=path.toUtf8();
    if(::stat(filename.constData(),&cached) || !S_ISREG(cached.st_mode) || cached.st_size>8*1024*1024)return false;
    auto file=std::unique_ptr<FILE,CloseFile>(std::fopen(filename.constData(),"rb"));
    if(!file)return false;
    png_structp png=png_create_read_struct(PNG_LIBPNG_VER_STRING,nullptr,pngError,pngWarning);
    if(!png)return false;
    png_infop info=png_create_info_struct(png);
    if(!info){png_destroy_read_struct(&png,nullptr,nullptr);return false;}
    // libpng conserva sus asignaciones; no se salta ningún destructor C++ al informar errores.
    if(setjmp(png_jmpbuf(png))){png_destroy_read_struct(&png,&info,nullptr);return false;}
    png_set_user_limits(png,1024,1024);png_set_chunk_malloc_max(png,256*1024);png_set_chunk_cache_max(png,32);
    png_init_io(png,file.get());
    png_read_png(png,info,PNG_TRANSFORM_EXPAND|PNG_TRANSFORM_STRIP_16|PNG_TRANSFORM_GRAY_TO_RGB,nullptr);
    png_textp texts=nullptr;const int count=png_get_text(png,info,&texts,nullptr);
    String storedUri,mtime,size;
    for(int i=0;i<count;++i){
        if(!texts[i].key || !texts[i].text)continue;
        if(!std::strcmp(texts[i].key,"Thumb::URI"))storedUri=String::fromUtf8(texts[i].text);
        else if(!std::strcmp(texts[i].key,"Thumb::MTime"))mtime=String::fromUtf8(texts[i].text);
        else if(!std::strcmp(texts[i].key,"Thumb::Size"))size=String::fromUtf8(texts[i].text);
    }
    bool ok=false;const auto seconds=mtime.toLongLong(&ok);
    bool valid=storedUri==uri && ok && seconds==original.st_mtime;
    if(!size.isEmpty()){const auto bytes=size.toLongLong(&ok);valid=valid && ok && bytes==original.st_size;}
    const int width=png_get_image_width(png,info),height=png_get_image_height(png,info),channels=png_get_channels(png,info);
    valid=valid && width>0 && height>0 && (channels==3 || channels==4) && !cancel->load();
    if(valid){
        const auto rows=png_get_rows(png,info);source.thumbnailRgb.resize(width*height*3);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            const auto *pixel=rows[y]+x*channels;const int alpha=channels==4?pixel[3]:255;
            for(int c=0;c<3;++c)source.thumbnailRgb[(y*width+x)*3+c]=char((pixel[c]*alpha+(c==2?128:0)*(255-alpha))/255);
        }
        source.thumbnailWidth=width;source.thumbnailHeight=height;
    }
    png_destroy_read_struct(&png,&info,nullptr);return valid;
}
}
bool desktopThumbnail(PreviewSource &source,const Cancellation &cancel)
{
    const auto path=source.path.toUtf8();struct stat original{};
    if(cancel->load())return false;
    bool checked=false;
    const auto uri=fileUri(source.path);const auto bytes=uri.toUtf8();unsigned char digest[EVP_MAX_MD_SIZE];unsigned length=0;
    if(!EVP_Digest(bytes.constData(),bytes.size(),digest,&length,EVP_md5(),nullptr) || length!=16)return false;
    String hash;for(unsigned i=0;i<length;++i)hash+=String("%1").arg(digest[i],2,16,Char('0'));
    auto cache=environment("XDG_CACHE_HOME");
    if(cache.isEmpty() || !cache.startsWith('/'))cache=DirectoryPath::homePath()+"/.cache";
    for(const auto &size:StringList{"xx-large","x-large","large","normal"}){
        if(cancel->load())return false;
        const auto thumbnail=cache+"/thumbnails/"+size+"/"+hash+".png";
        if(!FileInfo(thumbnail).isFile())continue;
        // Sin una miniatura local no se consulta el archivo de origen en la red.
        if(!checked){
            if(::stat(path.constData(),&original) || !S_ISREG(original.st_mode) || ::access(path.constData(),R_OK))return false;
            checked=true;
        }
        if(load(thumbnail,uri,original,source,cancel))return true;
    }
    return false;
}
std::shared_ptr<PreviewSource> refreshVideoThumbnail(const std::shared_ptr<PreviewSource> &source,const Cancellation &cancel)
{
    if(!source || source->kind!=PreviewKind::Video || !source->thumbnailRgb.isEmpty())return {};
    auto updated=std::make_shared<PreviewSource>(*source);
    if(!desktopThumbnail(*updated,cancel) || cancel->load())return {};
    updated->error.clear();updated->initialPage=0;
    if(!updated->videoProbed)updated->pages=2;
    updated->details="Desktop thumbnail (shared cache)";return updated;
}

}
