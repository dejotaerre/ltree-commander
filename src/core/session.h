#pragma once

#include "fs/scanner.h"
#include "core/filespec.h"
#include "core/sort.h"
#include <QMap>
#include <QSet>

namespace ltree {

enum class View { Tree, Directory, Branch, Showall, Global };

struct Directory {
    QString path;
    QStringList children;
    QVector<FileEntry> files;
    bool loaded = false;
    bool collapsed = false;
    QString error;
};

struct TreeRow {
    QString path;
    QString prefix;
    QString name;
};

struct BranchSize {
    quint64 bytes = 0, files = 0;
    bool complete = false, logged = false;
    QString label(int width = 14) const;
};

class Session {
public:
    explicit Session(QString root);
    QString root;
    QString directory;
    View view = View::Tree;
    int fileIndex = 0;
    QSet<QString> tags;
    QString treeSpell;
    QString fileSpell;
    bool tagsOnly = false;
    Filespec filespec;
    SortOptions sort;

    const Directory &currentDirectory() const;
    const QMap<QString, Directory> &directories() const { return directories_; }
    const QVector<TreeRow> &tree() const { return tree_; }
    const QVector<FileEntry> &files() const { return files_; }
    const QVector<FileEntry> &scopeFiles() const { return scope_; }
    const QMap<QString, BranchSize> &branchSizes() const;
    const FileEntry *currentFile() const;
    quint64 revision() const { return revision_; }
    int treeIndex() const;
    void apply(const ScanResult &scan, bool preserveTags);
    bool expandToParent(const ScanResult &scan);
    void selectDirectory(const QString &path);
    void move(int delta);
    void first();
    void last();
    void parent(bool allowRoot = true);
    void sibling(int direction);
    void enter(View target, bool onlyTagged = false);
    void returnToTree();
    void toggleCollapse(bool secondLevel);
    void unlog();
    void setTag(bool tagged, bool all);
    void invertTag();
    bool spell(const QString &text, bool next = false);
    void toggleTagsOnly();
    void rebuild(bool preserveFile = true);
    void syncLoggingFrom(const Session &source);
    static Session global(const QVector<Session> &sources, const Session &context, bool tagged = false);
    void syncGlobalFrom(const Session &source);
    void removeDirectory(const QString &path);
    void removeFile(const QString &path);
    void setFileWritable(const QString &path, bool writable);
    void renamePath(const QString &source, const QString &target);
    void graftPath(const QString &source, const QString &target);
    void setCompareFilter(const QSet<QString> &paths, SortKey key);
    void clearCompareFilter();
    bool hasCompareFilter() const { return compareFiltered_; }
    void tagBranch(int action);

private:
    quint64 revision_ = 0;
    QSet<QString> globalRoots_;
    bool compareFiltered_=false;
    QSet<QString> compareFiles_;
    SortOptions compareSort_;
    QMap<QString, Directory> directories_;
    QVector<TreeRow> tree_;
    QVector<FileEntry> files_;
    QVector<FileEntry> scope_;
    QSet<QString> visibleSnapshot_;
    mutable QMap<QString, BranchSize> branchSizeCache_;
    mutable bool branchSizeDirty_ = true;
    void appendTree(const QString &path, const QString &prefix, bool last, bool top);
    void removeBranch(const QString &path, bool includeRoot);
};

}
