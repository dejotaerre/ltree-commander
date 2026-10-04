#include "fs/filecompare.h"
#include "fs/viewdocument.h"
#include <QProcessEnvironment>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QElapsedTimer>
#include <algorithm>

namespace ltree {
CompareResult compareFiles(const QString &first, const QString &second, const Cancellation &cancel, const CompareOptions &options)
{
    CompareResult result;
    const auto a = readViewDocument(first,cancel,false), b = readViewDocument(second,cancel,false);
    if (cancel->load() || a.cancelled || b.cancelled) { result.cancelled=true; return result; }
    if (!a.error.isEmpty() || !b.error.isEmpty()) {
        result.error=!a.error.isEmpty()?"First file: "+a.error+" — "+first:"Second file: "+b.error+" — "+second; return result;
    }
    result.firstBytes=a.bytes; result.secondBytes=b.bytes;
    result.bytesEqual=a.bytes==b.bytes;
    const auto size=std::max(a.bytes.size(),b.bytes.size());
    for(qsizetype offset=0;offset<size;offset+=4){
        if((offset&4095)==0 && cancel->load()){result.cancelled=true;return result;}
        bool changed=false;
        for(qsizetype i=offset;i<std::min(size,offset+4);++i){
            if(i>=a.bytes.size() || i>=b.bytes.size() || a.bytes[i]!=b.bytes[i]){changed=true;++result.differentBytes;}
        }
        if(changed)result.binaryBlocks.append(int(offset));
    }
    const auto binary=[](const QString &text){
        for(const auto c:text)if(c.unicode()<32 && c!='\n' && c!='\r' && c!='\t')return true;
        return false;
    };
    result.rawCharacters=binary(a.text) || binary(b.text);
    const auto lines=[](QString text) {
        if (text.isEmpty()) return QStringList{};
        if (text.endsWith('\n')) text.chop(1);
        return text.split('\n');
    };
    const auto characters=[](const QByteArray &bytes){
        QString value=QString::fromLatin1(bytes);
        value.replace("\r\n","\n");value.replace('\r','\n');return value;
    };
    result.first=lines(result.rawCharacters?characters(a.bytes):a.text);
    result.second=lines(result.rawCharacters?characters(b.bytes):b.text);
    return compareCharacters(result,options,cancel);
}

CompareResult compareCharacters(const CompareResult &source, const CompareOptions &options, const Cancellation &cancel)
{
    CompareResult result=source;
    if(!result.error.isEmpty() || result.cancelled)return result;
    result.rows.clear();result.changes.clear();result.textError.clear();
    result.first.clear();result.second.clear();result.firstLineNumbers.clear();result.secondLineNumbers.clear();
    if(cancel->load()){result.cancelled=true;return result;}
    if (source.first.size()+source.second.size()>200000) {
        result.textError="Comparison limit: 200000 lines across both files"; return result;
    }
    QStringList firstKeys,secondKeys;
    const auto prepare=[&](const QStringList &lines,QStringList &shown,QStringList &keys,QVector<int> &numbers){
        for(int line=0;line<lines.size();++line){
            if((line&4095)==0 && cancel->load())return false;
            const auto &original=lines[line];
            if(options.suppressEmpty && std::all_of(original.cbegin(),original.cend(),[](QChar c){return c==' ' || c=='\t';}))continue;
            QString display=original;
            if(options.compressWhitespace){
                display.clear();bool space=false;
                for(const auto c:original){
                    if(c==' ' || c=='\t'){if(!space)display+=' ';space=true;}
                    else {display+=c;space=false;}
                }
            }
            auto key=display;
            if(!options.caseSensitive){
                if(source.rawCharacters){
                    // Sin selector de página de códigos, el plegado binario se limita a ASCII.
                    for(auto &c:key)if(c>='A' && c<='Z')c=QChar(c.unicode()+32);
                }else key=key.toCaseFolded();
            }
            shown.append(display);keys.append(key);numbers.append(line+1);
        }
        return true;
    };
    if(!prepare(source.first,result.first,firstKeys,result.firstLineNumbers) ||
       !prepare(source.second,result.second,secondKeys,result.secondLineNumbers)){
        result.cancelled=true;return result;
    }
    QTemporaryDir temporary;
    if (!temporary.isValid()) { result.textError="Cannot create the temporary comparison directory"; return result; }
    const auto snapshot=[&](const QString &name,const QStringList &content) {
        QFile file(temporary.filePath(name));
        if (!file.open(QIODevice::WriteOnly)) return false;
        const QByteArray bytes=content.isEmpty()?QByteArray{}:(content.join('\n')+'\n').toUtf8();
        return file.write(bytes)==bytes.size() && file.flush();
    };
    if (!snapshot("first",firstKeys) || !snapshot("second",secondKeys)) {
        result.textError="Cannot write the temporary copies"; return result;
    }
    QProcess diff;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    diff.setProcessEnvironment(environment);
    const QString format="%df %dn %dF %dN\n";
    diff.start("/usr/bin/diff", {"--text","--old-group-format="+format,"--new-group-format="+format,
        "--changed-group-format="+format,"--unchanged-group-format=","--",temporary.filePath("first"),temporary.filePath("second")});
    if (!diff.waitForStarted(3000)) { result.textError="Cannot start /usr/bin/diff: "+diff.errorString(); return result; }
    QElapsedTimer timer; timer.start();
    while (!diff.waitForFinished(50)) {
        if (cancel->load() || timer.elapsed()>30000) {
            diff.kill(); diff.waitForFinished(); result.cancelled=cancel->load();
            if (!result.cancelled) result.textError="Comparison timed out after 30 seconds";
            return result;
        }
    }
    if (cancel->load()) { result.cancelled=true; return result; }
    if (diff.exitStatus()!=QProcess::NormalExit || diff.exitCode()>1) {
        result.textError="diff error: "+QString::fromLocal8Bit(diff.readAllStandardError()); return result;
    }
    int left=0,right=0;
    const auto output=diff.readAllStandardOutput().split('\n');
    for (const auto &group:output) {
        if (group.isEmpty()) continue;
        const auto fields=group.split(' ');
        if (fields.size()!=4) { result.textError="Unexpected response from diff"; return result; }
        int n[4];
        for(int i=0;i<4;++i) { bool ok=false; n[i]=fields[i].toInt(&ok); if(!ok){result.textError="Invalid response from diff";return result;} }
        const int startLeft=n[0]-1,startRight=n[2]-1,countLeft=n[1],countRight=n[3];
        if (startLeft<left || startRight<right || startLeft-left!=startRight-right || countLeft<0 || countRight<0 ||
            startLeft+countLeft>result.first.size() || startRight+countRight>result.second.size()) {
            result.textError="Invalid ranges received from diff"; return result;
        }
        while(left<startLeft) result.rows.append({left++,right++,false});
        result.changes.append(int(result.rows.size()));
        for(int i=0;i<std::max(countLeft,countRight);++i)
            result.rows.append({i<countLeft?left+i:-1,i<countRight?right+i:-1,true});
        left+=countLeft; right+=countRight;
    }
    if (result.first.size()-left!=result.second.size()-right) { result.textError="Incomplete alignment from diff"; return result; }
    while(left<result.first.size()) result.rows.append({left++,right++,false});
    return result;
}
}
