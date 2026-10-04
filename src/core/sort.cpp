#include "core/sort.h"
#include <QFileInfo>
#include <algorithm>

namespace ltree {
namespace {
int cmp(const QString &a, const QString &b) { return QString::compare(a,b,Qt::CaseInsensitive); }
template<typename T> int cmpValue(const T &a,const T &b) { return a<b?-1:a>b?1:0; }
QString digitRun(const QString &text, int &position)
{
    const int start=position;
    while(position<text.size() && text[position]>='0' && text[position]<='9') ++position;
    QString digits=text.mid(start,position-start);
    while(digits.size()>1 && digits.front()=='0') digits.remove(0,1);
    return digits;
}
bool digit(QChar c) { return c>='0' && c<='9'; }
int numeric(const QString &a,const QString &b)
{
    int i=0,j=0;
    while(i<a.size() && j<b.size()) {
        if(digit(a[i]) && digit(b[j])) {
            const auto left=digitRun(a,i), right=digitRun(b,j);
            const int value=left.size()==right.size()?cmp(left,right):cmpValue(left.size(),right.size());
            if(value)return value;
        } else {
            const int value=cmp(QString(a[i++]),QString(b[j++]));
            if(value)return value;
        }
    }
    return cmpValue(a.size()-i,b.size()-j);
}
QString alpha(QString name)
{
    int start=0; while(start<name.size() && !name[start].isLetter())++start;
    return name.mid(start);
}
QString firstNumber(const QString &name)
{
    int start=0; while(start<name.size() && !digit(name[start]))++start;
    return digitRun(name,start);
}
struct Item { FileEntry file; QString base,ext,path; };
}
QString sortLabel(const SortOptions &options)
{
    const QStringList names{"Name","Ext","Date","Time","Length","Alpha","Number","Size","Unsorted","Value"};
    return names[int(options.key)]+(options.key==SortKey::Unsorted?QString{}:QString(options.descending?" ↓":" ↑")+(options.pathFirst?" Path":""));
}
void sortFiles(QVector<FileEntry> &files,const SortOptions &options,bool aggregate)
{
    if(options.key==SortKey::Unsorted)return;
    QVector<Item> items; items.reserve(files.size());
    for(const auto &file:files) {
        const int dot=int(file.name.lastIndexOf('.'));
        items.append({file,dot>0?file.name.left(dot):file.name,dot>0?file.name.mid(dot+1):QString{},QFileInfo(file.path).absolutePath()});
    }
    std::sort(items.begin(),items.end(),[&](const Item &a,const Item &b) {
        int result=0;
        if(aggregate && options.pathFirst)result=cmp(a.path,b.path);
        if(!result) switch(options.key) {
        case SortKey::Name: break;
        case SortKey::Extension: result=cmp(a.ext,b.ext); break;
        case SortKey::Date: result=cmpValue(a.file.modified,b.file.modified); break;
        case SortKey::Time:
            result=cmpValue(a.file.modified.toLocalTime().time(),b.file.modified.toLocalTime().time());
            if(!result)result=cmpValue(a.file.modified.toLocalTime().date(),b.file.modified.toLocalTime().date());
            break;
        case SortKey::Length: result=cmpValue(a.file.name.size(),b.file.name.size()); break;
        case SortKey::Alpha: result=cmp(alpha(a.base),alpha(b.base)); break;
        case SortKey::Number: result=numeric(a.base,b.base); break;
        case SortKey::Size: result=cmpValue(a.file.size,b.file.size); break;
        case SortKey::Value: {
            const auto left=firstNumber(a.base),right=firstNumber(b.base);
            result=left.size()==right.size()?cmp(left,right):cmpValue(left.size(),right.size());
            break;
        }
        case SortKey::Unsorted: break;
        }
        if(!result)result=cmp(a.base,b.base);
        if(!result)result=cmp(a.ext,b.ext);
        if(!result)result=cmp(a.path,b.path);
        if(!result)result=QString::compare(a.file.path,b.file.path,Qt::CaseSensitive);
        return options.descending?result>0:result<0;
    });
    for(int i=0;i<items.size();++i)files[i]=std::move(items[i].file);
}
}
