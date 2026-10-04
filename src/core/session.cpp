#include "core/session.h"
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <algorithm>
#include <limits>

namespace ltree {

QString BranchSize::label(int width) const
{
    if(!logged || (complete && files==0))return {};
    if(files==0)return "{ ? }";
    const QLocale locale(QLocale::Spanish,QLocale::Uruguay);
    const QStringList units{"k","M","G","T","P","E"};
    quint64 divisor=1024;
    for(int unit=0;unit<units.size();++unit){
        const auto value=bytes/divisor+(bytes%divisor!=0);
        const QString text=locale.toString(value)+units[unit];
        if(text.size()<=width-3 || unit==units.size()-1)
            return QString(complete?'[':'{')+' '+text.rightJustified(width-3)+(complete?']':'}');
        divisor*=1024;
    }
    return {};
}

const QMap<QString, BranchSize> &Session::branchSizes() const
{
    if(!branchSizeDirty_)return branchSizeCache_;
    branchSizeCache_.clear();
    const auto add=[](BranchSize &total,quint64 bytes){
        if(bytes>std::numeric_limits<quint64>::max()-total.bytes){
            total.bytes=std::numeric_limits<quint64>::max();total.complete=false;
        }else total.bytes+=bytes;
    };
    for(const auto &dir:directories_){
        BranchSize size;
        size.complete=dir.loaded && dir.error.isEmpty();
        size.logged=dir.loaded || !dir.files.isEmpty();
        size.files=quint64(dir.files.size());
        for(const auto &file:dir.files)add(size,quint64(std::max(qint64(0),file.size)));
        branchSizeCache_.insert(dir.path,size);
    }
    // Las rutas hijas ordenan después de su padre; acumular en sentido inverso incluye ramas colapsadas.
    for(auto it=directories_.cend();it!=directories_.cbegin();){
        --it;auto &size=branchSizeCache_[it.key()];
        for(const auto &child:it->children)if(!branchSizeCache_.contains(child))size.complete=false;
        if(it.key()==root)continue;
        const QString parent=QFileInfo(it.key()).absolutePath();
        const auto found=branchSizeCache_.find(parent);
        if(found==branchSizeCache_.end())continue;
        add(found.value(),size.bytes);found->files+=size.files;
        found->complete=found->complete && size.complete;found->logged=found->logged || size.logged;
    }
    branchSizeDirty_=false;
    return branchSizeCache_;
}

Session::Session(QString initialRoot)
    : root(QDir::cleanPath(QFileInfo(initialRoot).absoluteFilePath())), directory(root)
{
    Directory item;
    item.path = root;
    directories_.insert(root, item);
    rebuild();
}

const Directory &Session::currentDirectory() const { return directories_.constFind(directory).value(); }

const FileEntry *Session::currentFile() const
{
    return fileIndex >= 0 && fileIndex < files_.size() ? &files_[fileIndex] : nullptr;
}

int Session::treeIndex() const
{
    for (int i = 0; i < tree_.size(); ++i) if (tree_[i].path == directory) return i;
    return 0;
}

void Session::removeBranch(const QString &path, bool includeRoot)
{
    for (auto it = directories_.begin(); it != directories_.end();) {
        if (isWithin(it.key(), path) && (includeRoot || it.key() != path)) it = directories_.erase(it);
        else ++it;
    }
    for (auto it = tags.begin(); it != tags.end();) {
        if (isWithin(*it, path)) it = tags.erase(it); else ++it;
    }
}

void Session::apply(const ScanResult &scan, bool preserveTags)
{
    clearCompareFilter();
    for (const auto &item : scan.directories) {
        if (!isWithin(item.path, root)) continue;
        if(view==View::Global && std::none_of(globalRoots_.cbegin(),globalRoots_.cend(),[&](const auto &path){return isWithin(item.path,path);}))continue;
        const auto old = directories_.value(item.path);
        // Un error de lectura conserva los datos previos para no perder marcas sin evidencia.
        if (!item.error.isEmpty()) {
            auto &dir = directories_[item.path];
            dir.path = item.path;
            dir.error = item.error;
            continue;
        }
        for (const auto &child : old.children) {
            if (!item.children.contains(child)) removeBranch(child, true);
        }
        auto &dir = directories_[item.path];
        dir = {item.path, item.children, item.files, true, false, {}};
        for (const auto &child : item.children) {
            if (!directories_.contains(child)) {
                Directory next;
                next.path = child;
                directories_.insert(child, next);
            }
        }
        QSet<QString> present;
        for (const auto &file : item.files) present.insert(file.path);
        for (auto it = tags.begin(); it != tags.end();) {
            if (QFileInfo(*it).absolutePath() == item.path && (!preserveTags || !present.contains(*it)))
                it = tags.erase(it);
            else ++it;
        }
    }
    if (!directories_.contains(directory)) directory = root;
    if (tagsOnly) {
        visibleSnapshot_ = tags;
        if (visibleSnapshot_.isEmpty()) tagsOnly = false;
    }
    rebuild();
}

bool Session::expandToParent(const ScanResult &scan)
{
    const QString parentPath = QFileInfo(root).absolutePath();
    if (root == parentPath || scan.cancelled || scan.directories.size() != 1 ||
        scan.directories.first().path != parentPath || !scan.directories.first().error.isEmpty()) return false;
    root = parentPath;
    apply(scan, true);
    returnToTree();
    selectDirectory(root);
    return true;
}

void Session::removeDirectory(const QString &path)
{
    if(path==root || !isWithin(path,root) || !directories_.contains(path))return;
    const int index=treeIndex();
    const bool selected=isWithin(directory,path);
    const QString parent=QFileInfo(path).absolutePath();
    if(directories_.contains(parent))directories_[parent].children.removeAll(path);
    removeBranch(path,true);
    while(!directories_.contains(directory) && directory!=root)directory=QFileInfo(directory).absolutePath();
    rebuild();
    if(selected && view==View::Tree)selectDirectory(tree_[std::min(index,int(tree_.size())-1)].path);
}

void Session::removeFile(const QString &path)
{
    const auto parent=directories_.find(QFileInfo(path).absolutePath());
    if(parent!=directories_.end())for(auto it=parent->files.begin();it!=parent->files.end();){
        if(it->path==path)it=parent->files.erase(it);else ++it;
    }
    tags.remove(path);visibleSnapshot_.remove(path);rebuild();
}

void Session::setFileWritable(const QString &path, bool writable)
{
    const auto parent=directories_.find(QFileInfo(path).absolutePath());
    if(parent==directories_.end())return;
    for(auto &file:parent->files)if(file.path==path){file.writable=writable;rebuild();return;}
}

void Session::renamePath(const QString &source,const QString &target)
{
    clearCompareFilter();
    const QString selected=currentFile()?currentFile()->path:QString{};
    const auto map=[&](const QString &path){return isWithin(path,source)?target+path.mid(source.size()):path;};
    root=map(root);directory=map(directory);
    QMap<QString,Directory> renamed;
    for(auto dir:directories_){
        dir.path=map(dir.path);
        for(auto &child:dir.children)child=map(child);
        for(auto &file:dir.files){file.path=map(file.path);file.name=QFileInfo(file.path).fileName();file.hidden=file.name.startsWith('.');}
        renamed.insert(dir.path,dir);
    }
    directories_=renamed;
    const auto mapSet=[&](const QSet<QString> &set){QSet<QString> mapped;for(const auto &path:set)mapped.insert(map(path));return mapped;};
    tags=mapSet(tags);visibleSnapshot_=mapSet(visibleSnapshot_);rebuild(false);
    for(int i=0;i<files_.size();++i)if(files_[i].path==map(selected)){fileIndex=i;break;}
}

void Session::graftPath(const QString &source, const QString &target)
{
    const QString oldParent = QFileInfo(source).absolutePath();
    if (directories_.contains(oldParent)) directories_[oldParent].children.removeAll(source);
    renamePath(source, target);
    QSet<QString> roots;
    for (const auto &path : globalRoots_) roots.insert(isWithin(path, source) ? target + path.mid(source.size()) : path);
    globalRoots_ = roots;
    if (view != View::Global && !isWithin(directory, root)) root = QFileInfo(target).absolutePath();
    if (view != View::Global) {
        for (auto it = directories_.begin(); it != directories_.end();)
            if (!isWithin(it.key(), root)) it = directories_.erase(it); else ++it;
        for (auto it = tags.begin(); it != tags.end();)
            if (!isWithin(*it, root)) it = tags.erase(it); else ++it;
    }
    // Conectar los padres nuevos conserva la carga de la rama y su selección.
    if (isWithin(target, root) && directories_.contains(target)) {
        QString child = target;
        while (child != root) {
            const QString parent = QFileInfo(child).absolutePath();
            if (!directories_.contains(parent)) { Directory dir;dir.path = parent;directories_.insert(parent, dir); }
            auto &children = directories_[parent].children;
            if (!children.contains(child)) children.append(child);
            std::sort(children.begin(), children.end(), [](const QString &a, const QString &b) {
                const int value = QString::compare(a, b, Qt::CaseInsensitive);return value ? value < 0 : a < b;
            });
            child = parent;
        }
    }
    rebuild();
}

void Session::setCompareFilter(const QSet<QString> &paths,SortKey key)
{
    if(!compareFiltered_)compareSort_=sort;
    compareFiltered_=true;compareFiles_=paths;sort.key=key;sort.descending=false;sort.pathFirst=false;rebuild();
}
void Session::clearCompareFilter()
{
    if(!compareFiltered_)return;
    compareFiltered_=false;compareFiles_.clear();sort=compareSort_;rebuild();
}
void Session::tagBranch(int action)
{
    for(const auto &dir:directories_)if(dir.loaded&&isWithin(dir.path,directory))for(const auto &file:dir.files)if(filespec.matches(file)){
        if(action==0||(action==2&&tags.contains(file.path)))tags.remove(file.path);else tags.insert(file.path);
    }
    rebuild();
}

void Session::appendTree(const QString &path, const QString &prefix, bool isLast, bool top)
{
    const auto found = directories_.constFind(path);
    const auto &dir = found.value();
    tree_.append({path, top ? QString{} : prefix + (isLast ? "└─" : "├─"),
                  top ? root : QFileInfo(path).fileName()});
    if (dir.collapsed) return;
    const QString childPrefix = top ? QString{} : prefix + (isLast ? "  " : "│ ");
    for (int i = 0; i < dir.children.size(); ++i)
        appendTree(dir.children[i], childPrefix, i == dir.children.size() - 1, false);
}

void Session::rebuild(bool preserveFile)
{
    ++revision_;
    branchSizeDirty_=true;
    const QString selected = preserveFile && currentFile() ? currentFile()->path : QString{};
    tree_.clear();
    appendTree(root, {}, true, true);
    scope_.clear();
    for (const auto &dir : directories_) {
        const bool include = view == View::Global || view == View::Showall ||
            (view == View::Branch ? isWithin(dir.path, directory) : dir.path == directory);
        if (include && dir.loaded) scope_ += dir.files;
    }
    sortFiles(scope_, sort, view == View::Branch || view == View::Showall || view == View::Global);
    files_.clear();
    for (const auto &file : scope_)
        if (filespec.matches(file) && (!tagsOnly || visibleSnapshot_.contains(file.path)) && (!compareFiltered_ || compareFiles_.contains(file.path))) files_.append(file);
    fileIndex = std::clamp(fileIndex, 0, std::max(0, int(files_.size()) - 1));
    if (!selected.isEmpty()) {
        for (int i = 0; i < files_.size(); ++i) if (files_[i].path == selected) { fileIndex = i; break; }
    }
}

void Session::selectDirectory(const QString &path)
{
    clearCompareFilter();
    if (!directories_.contains(path)) return;
    directory = path;
    fileIndex = 0;
    rebuild(false);
}

void Session::syncLoggingFrom(const Session &source)
{
    if (root != source.root) return;
    const int index=treeIndex();
    const bool removed=!source.directories_.contains(directory);
    directories_ = source.directories_;
    QSet<QString> present;
    for (const auto &dir : directories_) for (const auto &file : dir.files) present.insert(file.path);
    tags.intersect(present);
    visibleSnapshot_.intersect(present);
    while (!directories_.contains(directory) && directory != root) directory = QFileInfo(directory).absolutePath();
    rebuild();
    if(removed && view==View::Tree)selectDirectory(tree_[std::min(index,int(tree_.size())-1)].path);
    // Si la selección quedó dentro de una rama colapsada, se muestra el ancestro visible.
    const auto visible = [this] {
        for (const auto &row : tree_) if (row.path == directory) return true;
        return false;
    };
    if (view == View::Tree) while (!visible() && directory != root) {
        directory = QFileInfo(directory).absolutePath();
        rebuild();
    }
}

Session Session::global(const QVector<Session> &sources, const Session &context, bool tagged)
{
    Session result("/");
    result.filespec=context.filespec;result.sort=context.sort;result.directory=context.directory;
    QMap<QString,const Session *> owners;
    for(const auto &source:sources){
        result.globalRoots_.insert(source.root);
        for(auto it=source.directories_.cbegin();it!=source.directories_.cend();++it){
            if(it->loaded || !result.directories_.contains(it.key()))result.directories_.insert(it.key(),it.value());
            if(it->loaded)owners.insert(it.key(),&source);
        }
    }
    for(const auto &source:sources)for(const auto &tag:source.tags)
        if(owners.value(QFileInfo(tag).absolutePath())==&source)result.tags.insert(tag);
    result.view=View::Global;result.tagsOnly=tagged;result.visibleSnapshot_=result.tags;result.rebuild(false);
    return result;
}

void Session::syncGlobalFrom(const Session &source)
{
    // La vista global publica cambios solo en directorios que esta sesión ya conoce.
    QSet<QString> changed, replacements;
    for(auto it=directories_.begin();it!=directories_.end();++it){
        const auto found=source.directories_.constFind(it.key());
        if(found==source.directories_.cend() || !found->loaded)continue;
        const bool collapsed=it->collapsed;it.value()=found.value();it->collapsed=collapsed;
        changed.insert(it.key());
        for(const auto &file:it->files)if(source.tags.contains(file.path))replacements.insert(file.path);
    }
    for(auto tag=tags.begin();tag!=tags.end();){if(changed.contains(QFileInfo(*tag).absolutePath()))tag=tags.erase(tag);else ++tag;}
    tags.unite(replacements);
    rebuild();
}

void Session::move(int delta)
{
    if (view == View::Tree) {
        const int index = std::clamp(treeIndex() + delta, 0, std::max(0, int(tree_.size()) - 1));
        selectDirectory(tree_[index].path);
    } else fileIndex = std::clamp(fileIndex + delta, 0, std::max(0, int(files_.size()) - 1));
}

void Session::first() { move(-int(tree_.size()) - int(files_.size())); }
void Session::last() { move(int(tree_.size()) + int(files_.size())); }

void Session::parent(bool allowRoot)
{
    const QString target = QFileInfo(directory).absolutePath();
    if (directory != root && (allowRoot || target != root)) selectDirectory(target);
}

void Session::sibling(int direction)
{
    const QString parentPath = QFileInfo(directory).absolutePath();
    if (!directories_.contains(parentPath)) return;
    const auto siblings = directories_.value(parentPath).children;
    const int index = int(siblings.indexOf(directory)) + direction;
    if (index >= 0 && index < siblings.size()) selectDirectory(siblings[index]);
}

void Session::enter(View target, bool onlyTagged)
{
    clearCompareFilter();
    const auto oldView = view;
    view = target;
    tagsOnly = false;
    fileIndex = 0;
    rebuild(false);
    if (onlyTagged) {
        visibleSnapshot_.clear();
        for (const auto &file : files_) if (tags.contains(file.path)) visibleSnapshot_.insert(file.path);
        if (visibleSnapshot_.isEmpty()) { view = oldView; rebuild(false); return; }
        tagsOnly = true;
        rebuild(false);
    }
    if (files_.isEmpty()) { view = oldView; tagsOnly = false; rebuild(false); }
}

void Session::returnToTree()
{
    clearCompareFilter();
    view = View::Tree;
    tagsOnly = false;
    visibleSnapshot_.clear();
    rebuild();
}

void Session::toggleCollapse(bool secondLevel)
{
    auto &dir = directories_[directory];
    if (!secondLevel) dir.collapsed = !dir.collapsed;
    else {
        bool shouldCollapse = false;
        for (const auto &child : dir.children) if (!directories_[child].collapsed) shouldCollapse = true;
        for (const auto &child : dir.children) directories_[child].collapsed = shouldCollapse;
    }
    rebuild();
}

void Session::unlog()
{
    removeBranch(directory, false);
    directories_[directory] = Directory{directory, {}, {}, false, false, {}};
    rebuild();
}

void Session::setTag(bool tagged, bool all)
{
    QStringList targets;
    if (view == View::Tree) {
        for (const auto &dir : directories_)
            if (all || dir.path == directory)
                for (const auto &file : dir.files) if (filespec.matches(file)) targets.append(file.path);
    } else if (all) {
        for (const auto &file : files_) targets.append(file.path);
    } else if (currentFile()) targets.append(currentFile()->path);
    for (const auto &path : targets) {
        if (tagged) tags.insert(path); else tags.remove(path);
    }
    if (!all) move(1);
}

void Session::invertTag()
{
    if (view != View::Tree && currentFile()) {
        const QString path = currentFile()->path;
        if (tags.contains(path)) tags.remove(path); else tags.insert(path);
    }
}

bool Session::spell(const QString &text, bool next)
{
    if (text.isEmpty()) return false;
    const int count = view == View::Tree ? int(tree_.size()) : int(files_.size());
    const int start = view == View::Tree ? treeIndex() : fileIndex;
    for (int j = next ? 1 : 0; j < count + (next ? 1 : 0); ++j) {
        const int i = (start + j) % count;
        const QString name = view == View::Tree ? QFileInfo(tree_[i].path).fileName() : files_[i].name;
        if (name.startsWith(text, Qt::CaseInsensitive)) {
            if (view == View::Tree) selectDirectory(tree_[i].path); else fileIndex = i;
            return true;
        }
    }
    return false;
}

void Session::toggleTagsOnly()
{
    QSet<QString> matching;
    for (const auto &file : files_) if (tags.contains(file.path)) matching.insert(file.path);
    if (matching.isEmpty() || matching.size() == files_.size()) {
        if (!tagsOnly) return;
        tagsOnly = false;
    } else {
        tagsOnly = true;
        visibleSnapshot_ = matching;
    }
    rebuild();
}

}
