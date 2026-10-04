#pragma once
#include <QDir>
#include <QFile>
#include <QUuid>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ltree::safeio {
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() { if(value >= 0) ::close(value); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
};
inline QString error() { return QString::fromLocal8Bit(std::strerror(errno)); }
inline int directory(const QString &path, bool create = false)
{
    if(!QDir::isAbsolutePath(path) || path.contains(QChar(0))) { errno=EINVAL; return -1; }
    int fd=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    for(const auto &part:QDir::cleanPath(path).split('/',Qt::SkipEmptyParts)) {
        const auto name=QFile::encodeName(part);
        int next=::openat(fd,name.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(next<0 && errno==ENOENT && create) {
            if(::mkdirat(fd,name.constData(),0777)<0 && errno!=EEXIST) { const int e=errno;::close(fd);errno=e;return -1; }
            next=::openat(fd,name.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        }
        const int e=errno;::close(fd);fd=next;errno=e;
        if(fd<0) return -1;
    }
    return fd;
}
inline QByteArray temporaryName() { return (".ltree-"+QUuid::createUuid().toString(QUuid::WithoutBraces)).toLatin1(); }
inline bool publish(int parent,const QByteArray &temporary,const QByteArray &name,bool replace)
{
    struct stat st{};
    if(::fstatat(parent,name.constData(),&st,AT_SYMLINK_NOFOLLOW)==0 && (!S_ISREG(st.st_mode) || !replace)) { errno=EEXIST;return false; }
    if(replace) return ::renameat(parent,temporary.constData(),parent,name.constData())==0;
    return ::syscall(SYS_renameat2,parent,temporary.constData(),parent,name.constData(),1)==0;
}
}
