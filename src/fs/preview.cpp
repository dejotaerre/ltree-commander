#include "fs/preview.h"
#include "fs/thumbnail.h"
#include "fs/fontpreview.h"
#include <algorithm>
#include <cctype>
#include <csignal>
#include <cmath>
#include <sys/resource.h>
#include <unistd.h>

namespace ltree {
namespace {
struct ReaderResult { Bytes output; String error; };
ReaderResult reader(const String &program, const StringList &args, const Cancellation &cancel, int timeout,const String &workingDirectory={})
{
    ReaderResult result;
    if (cancel->load()) return result;
    Process process;
    if(!workingDirectory.isEmpty())process.setWorkingDirectory(workingDirectory);
    auto env = ProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C.UTF-8");env.insert("SAL_USE_VCLPLUGIN", "svp");
    process.setProcessEnvironment(env);
    process.setChildProcessModifier([] {
        ::setsid();
        const rlimit memory{4ULL*1024*1024*1024,4ULL*1024*1024*1024}, file{64ULL*1024*1024,64ULL*1024*1024}, cpu{25,25};
        ::setrlimit(RLIMIT_AS,&memory);::setrlimit(RLIMIT_FSIZE,&file);::setrlimit(RLIMIT_CPU,&cpu);
    });
    process.start(program,args);
    if (!process.waitForStarted(3000)) { result.error="Cannot start preview reader: "+FileInfo(program).fileName();return result; }
    const auto pid=process.processId();ElapsedTimer elapsed;elapsed.start();
    const auto stop=[&] {
        // El grupo contiene únicamente el lector y sus propios procesos auxiliares.
        if(pid>0)::kill(-pid,SIGKILL);
        process.kill();process.waitForFinished(1000);
    };
    Bytes errors;
    while(process.state()!=Process::NotRunning){
        process.waitForFinished(40);
        result.output+=process.readAllStandardOutput();errors+=process.readAllStandardError();
        if(errors.size()>8192)errors=errors.left(8192);
        if(cancel->load()){stop();result.output.clear();return result;}
        if(elapsed.elapsed()>timeout || result.output.size()>16*1024*1024){
            stop();result.output.clear();result.error="Preview reader exceeded its time or output limit";return result;
        }
    }
    result.output+=process.readAllStandardOutput();
    if(result.output.size()>16*1024*1024){result.output.clear();result.error="Preview image is too large";return result;}
    if(process.exitStatus()!=Process::NormalExit || process.exitCode()!=0)
        result.error="Cannot preview this file (damaged, encrypted or unsupported format)";
    return result;
}
PreviewRaster ppm(const Bytes &data)
{
    PreviewRaster result;Index at=0;
    const auto token=[&]() {
        while(at<data.size()){
            if(std::isspace(static_cast<unsigned char>(data[at]))){++at;continue;}
            if(data[at]=='#'){while(at<data.size() && data[at]!='\n')++at;continue;}
            break;
        }
        const auto start=at;while(at<data.size() && !std::isspace(static_cast<unsigned char>(data[at])))++at;
        return data.mid(start,at-start);
    };
    const auto magic=token();bool widthOk=false,heightOk=false;
    result.width=token().toInt(&widthOk);result.height=token().toInt(&heightOk);const auto depth=token();
    if(magic!="P6" || !widthOk || !heightOk || result.width<1 || result.height<1 || result.width>2048 || result.height>2048 || depth!="255" || at>=data.size()){
        result.error="The preview reader returned an invalid image";return result;
    }
    const char separator=data[at++];if(separator=='\r' && at<data.size() && data[at]=='\n')++at;
    const Index count=Index(result.width)*result.height*3;
    if(data.size()-at!=count){result.error="The preview image is incomplete";return result;}
    result.rgb=data.mid(at,count);return result;
}
}
PreviewKind previewKind(const String &path)
{
    const auto extension=FileInfo(path).suffix().toLower();
    if(extension=="pdf")return PreviewKind::Pdf;
    if(StringList{"ttf","otf","ttc","otc","woff","woff2","pfa","pfb","bdf","pcf","pf2"}.contains(extension))return PreviewKind::Font;
    if(StringList{"mkv","mp4","avi","webm","mov","m4v","mpg","mpeg","m2v","mts","m2ts","wmv","flv","3gp","ogv","vob"}.contains(extension))return PreviewKind::Video;
    if(StringList{"docx","doc","odt","rtf","xlsx","xls","ods","pptx","ppt","odp"}.contains(extension))return PreviewKind::Office;
    if(StringList{"png","jpg","jpeg","gif","bmp","webp","tif","tiff","avif","heic","ico","svg","ppm","pgm","pbm"}.contains(extension))return PreviewKind::Image;
    return PreviewKind::None;
}
namespace {
void probeVideo(const std::shared_ptr<PreviewSource> &source,const Cancellation &cancel)
{
    const auto probe=Paths::findExecutable("ffprobe");
    if(probe.isEmpty() || Paths::findExecutable("ffmpeg").isEmpty()){source->error="Video preview requires FFmpeg (ffmpeg and ffprobe)";return;}
    const auto metadata=reader(probe,{"-v","error","-protocol_whitelist","file,pipe","-select_streams","V:0","-show_entries","stream=codec_name,width,height,duration:format=duration","-of","default=noprint_wrappers=1",source->path},cancel,15000);
    if(cancel->load())return;
    if(!metadata.error.isEmpty()){source->error=metadata.error;return;}
    Map<String,String> values;
    for(const auto &line:String::fromUtf8(metadata.output).split('\n')){
        const auto equal=line.indexOf('=');if(equal>0 && line.mid(equal+1)!="N/A")values[line.left(equal)]=line.mid(equal+1).trimmed();
    }
    const int width=values.value("width").toInt(),height=values.value("height").toInt();
    if(width<=0 || height<=0){source->error="This file has no playable video stream";return;}
    const double duration=values.value("duration").toDouble();
    if(std::isfinite(duration) && duration>0 && duration<365*86400.0)source->durationMs=int64(duration*1000);
    // Las posiciones avanzan diez segundos y terminan antes del final del archivo.
    source->pages=1+int((std::max<int64>(0,source->durationMs-1000)+9999)/10000);
    source->initialPage=source->pages>2?2:1;
    source->details=String("%1x%2  %3").arg(width).arg(height).arg(values.value("codec_name").toUpper());
    source->videoProbed=true;
}
}
std::shared_ptr<PreviewSource> preparePreview(const String &path, const Cancellation &cancel)
{
    auto source=std::make_shared<PreviewSource>();source->path=FileInfo(path).absoluteFilePath();source->kind=previewKind(path);
    if(cancel->load() || source->kind==PreviewKind::None)return source;
    if(!FileInfo(path).isFile()){source->error="Preview requires a regular file";return source;}
    if(source->kind==PreviewKind::Image)return source;
    if(source->kind==PreviewKind::Font){prepareFontPreview(*source,cancel);return source;}
    if(source->kind==PreviewKind::Video){
        if(desktopThumbnail(*source,cancel)){
            source->initialPage=0;source->pages=2;source->details="Desktop thumbnail (shared cache)";return source;
        }
        probeVideo(source,cancel);return source;
    }
    source->pdf=source->path;
    if(source->kind==PreviewKind::Office){
        auto office=Paths::findExecutable("libreoffice");if(office.isEmpty())office=Paths::findExecutable("soffice");
        if(office.isEmpty()){source->error="Office preview requires LibreOffice; F12 switches to the raw viewer";return source;}
        source->temporary=std::make_shared<TemporaryDirectory>();
        if(!source->temporary->isValid()){source->error="Cannot create a temporary document preview";return source;}
        const auto profile=source->temporary->filePath("profile");DirectoryPath().mkpath(profile+"/user");
        File settings(profile+"/user/registrymodifications.xcu");
        if(!settings.open(IO::WriteOnly)){source->error="Cannot initialize the isolated document reader";return source;}
        settings.write("<?xml version=\"1.0\"?><oor:items xmlns:oor=\"http://openoffice.org/2001/registry\"><item oor:path=\"/org.openoffice.Office.Common/Security/Scripting\"><prop oor:name=\"DisableMacrosExecution\" oor:op=\"fuse\"><value>true</value></prop><prop oor:name=\"DisableActiveContent\" oor:op=\"fuse\"><value>true</value></prop></item></oor:items>");settings.close();
        File java(profile+"/java.xml");
        if(!java.open(IO::WriteOnly)){source->error="Cannot initialize the isolated document reader";return source;}
        java.write("<?xml version=\"1.0\"?><java xmlns=\"http://openoffice.org/2004/java/framework/1.0\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"><enabled xsi:nil=\"false\">false</enabled></java>");java.close();
        String uri;
        for(const unsigned char ch:profile.toUtf8()){
            if(std::isalnum(ch) || ch=='/' || ch=='-' || ch=='_' || ch=='.' || ch=='~')uri+=Char(ch);
            else uri+="%"+String("%1").arg(ch,2,16,Char('0')).toUpper();
        }
        StringList arguments{"-env:UserInstallation=file://"+uri,"-env:UNO_JAVA_JFW_USER_DATA=file://"+uri+"/java.xml","--headless","--norestore","--nodefault"};
        // Algunos DOCX de TextMaker declaran un tipo propio que impide la detección automática.
        if(FileInfo(path).suffix().toLower()=="docx")arguments.append("--infilter=Office Open XML Text");
        arguments.append({"--convert-to","pdf","--outdir",source->temporary->path(),source->path});
        const auto conversion=reader(office,arguments,cancel,30000,source->temporary->path());
        if(cancel->load())return source;
        source->pdf=source->temporary->filePath(FileInfo(path).completeBaseName()+".pdf");
        if(!conversion.error.isEmpty() || !File::exists(source->pdf)){
            source->error=conversion.error.isEmpty()?String("LibreOffice could not render this document"):String("Office preview: ")+conversion.error;
            return source;
        }
    }
    const auto info=Paths::findExecutable("pdfinfo");
    if(info.isEmpty() || Paths::findExecutable("pdftoppm").isEmpty()){source->error="PDF preview requires pdfinfo and pdftoppm (Poppler)";return source;}
    const auto metadata=reader(info,{source->pdf},cancel,10000);
    if(!metadata.error.isEmpty()){source->error=metadata.error;return source;}
    bool found=false;
    for(const auto &line:String::fromUtf8(metadata.output).split('\n'))if(line.startsWith("Pages:")){
        bool ok=false;const int pages=line.mid(6).trimmed().toInt(&ok);if(ok && pages>0){source->pages=pages;found=true;}break;
    }
    if(!found && !cancel->load())source->error="Cannot read the document page count";
    return source;
}
PreviewRaster renderPreview(const std::shared_ptr<PreviewSource> &source,int page,int width,int height,const Cancellation &cancel)
{
    PreviewRaster result;result.source=source;result.page=std::clamp(page<0?source->initialPage:page,source->thumbnailRgb.isEmpty()?1:0,source->pages);
    if(cancel->load()){result.cancelled=true;return result;}
    if(!source->error.isEmpty()){result.error=source->error;return result;}
    if(source->kind==PreviewKind::Font)return renderFontPreview(source,result.page,cancel);
    if(source->kind==PreviewKind::Video && !source->videoProbed){
        if(result.page==0){result.rgb=source->thumbnailRgb;result.width=source->thumbnailWidth;result.height=source->thumbnailHeight;return result;}
        auto probed=std::make_shared<PreviewSource>(*source);probeVideo(probed,cancel);
        return renderPreview(probed,page,width,height,cancel);
    }
    if(source->kind==PreviewKind::Video && result.page==0){result.rgb=source->thumbnailRgb;result.width=source->thumbnailWidth;result.height=source->thumbnailHeight;return result;}
    width=std::clamp(width,1,2048);height=std::clamp(height,1,2048);
    ReaderResult data;
    if(source->kind==PreviewKind::Video){
        const auto position=std::min<int64>(int64(result.page-1)*10000,std::max<int64>(0,source->durationMs-1000));
        const auto time=String::number(position/1000)+"."+String::number(position%1000).rightJustified(3,Char('0'));
        data=reader(Paths::findExecutable("ffmpeg"),{"-v","error","-nostdin","-protocol_whitelist","file,pipe","-threads","1","-filter_threads","1","-ss",time,"-i",source->path,"-map","0:V:0","-frames:v","1","-an","-sn","-dn","-vf",String("scale=w='max(1,min(%1,%2*dar))':h='max(1,min(%2,%1/dar))',setsar=1").arg(width).arg(height),"-pix_fmt","rgb24","-c:v","ppm","-f","image2pipe","pipe:1"},cancel,20000);
    }else if(source->kind==PreviewKind::Image){
        auto magick=Paths::findExecutable("magick");if(magick.isEmpty())magick=Paths::findExecutable("convert");
        if(magick.isEmpty()){result.error="Terminal image preview requires ImageMagick (magick or convert)";return result;}
        data=reader(magick,{"-limit","memory","128MiB","-limit","map","128MiB",source->path+"[0]","-auto-orient","-thumbnail",String("%1x%2>").arg(width).arg(height),"-background","#000080","-alpha","remove","-depth","8","ppm:-"},cancel,10000);
    }else data=reader(Paths::findExecutable("pdftoppm"),{"-f",String::number(result.page),"-l",String::number(result.page),"-singlefile","-scale-to",String::number(std::max(width,height)),source->pdf},cancel,10000);
    if(cancel->load()){result.cancelled=true;return result;}
    if(!data.error.isEmpty()){result.error=data.error;return result;}
    auto image=ppm(data.output);image.source=source;image.page=result.page;return image;
}
String previewPosition(const PreviewSource &source,int page)
{
    if(source.kind==PreviewKind::Video && page==0)return "Desktop thumbnail";
    if(source.kind==PreviewKind::Font && page==1)return String("Specimen 1/%1").arg(source.pages);
    if(source.kind!=PreviewKind::Video)return String("Page %1/%2").arg(page).arg(source.pages);
    const auto clock=[](int64 milliseconds){
        const auto seconds=milliseconds/1000;
        return String("%1:%2:%3").arg(seconds/3600,2,10,Char('0')).arg(seconds/60%60,2,10,Char('0')).arg(seconds%60,2,10,Char('0'));
    };
    const auto position=std::min<int64>(int64(std::max(1,page)-1)*10000,std::max<int64>(0,source.durationMs-1000));
    return clock(position)+" / "+(source.durationMs>0?clock(source.durationMs):String("--:--:--"));
}
}
