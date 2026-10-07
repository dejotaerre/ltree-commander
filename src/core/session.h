#pragma once
#include "platform/platform.h"

#include "fs/scanner.h"
#include "core/filespec.h"
#include "core/sort.h"

namespace ltree {

enum class View { Tree, Directory, Branch, Showall, Global };

struct Directory {
    String path;
    StringList children;
    Vector<FileEntry> files;
    bool loaded = false;
    bool collapsed = false;
    String error;
};

struct TreeRow {
    String path;
    String prefix;
    String name;
};

struct BranchSize {
    uint64 bytes = 0, files = 0;
    bool complete = false, logged = false;
    String label(int width = 14) const;
};

class Session {
public:
    explicit Session(String root);
    String root;
    String directory;
    View view = View::Tree;
    int fileIndex = 0;
    Set<String> tags;
    String treeSpell;
    String fileSpell;
    bool tagsOnly = false;
    Filespec filespec;
    SortOptions sort;

    const Directory &currentDirectory() const;
    const Map<String, Directory> &directories() const { return directories_; }
    const Vector<TreeRow> &tree() const { return tree_; }
    const Vector<FileEntry> &files() const { return files_; }
    const Vector<FileEntry> &scopeFiles() const { return scope_; }
    const Map<String, BranchSize> &branchSizes() const;
    const FileEntry *currentFile() const;
    uint64 revision() const { return revision_; }
    int treeIndex() const;
    void apply(const ScanResult &scan, bool preserveTags);
    bool expandToParent(const ScanResult &scan);
    void selectDirectory(const String &path);
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
    bool spell(const String &text, bool next = false);
    void toggleTagsOnly();
    void rebuild(bool preserveFile = true);
    void syncLoggingFrom(const Session &source);
    static Session global(const Vector<Session> &sources, const Session &context, bool tagged = false);
    void syncGlobalFrom(const Session &source);
    void removeDirectory(const String &path);
    void removeFile(const String &path);
    void setFileWritable(const String &path, bool writable);
    void renamePath(const String &source, const String &target);
    void graftPath(const String &source, const String &target);
    void setCompareFilter(const Set<String> &paths, SortKey key);
    void clearCompareFilter();
    bool hasCompareFilter() const { return compareFiltered_; }
    void tagBranch(int action);

private:
    uint64 revision_ = 0;
    Set<String> globalRoots_;
    bool compareFiltered_=false;
    Set<String> compareFiles_;
    SortOptions compareSort_;
    Map<String, Directory> directories_;
    Vector<TreeRow> tree_;
    Vector<FileEntry> files_;
    Vector<FileEntry> scope_;
    Set<String> visibleSnapshot_;
    mutable Map<String, BranchSize> branchSizeCache_;
    mutable bool branchSizeDirty_ = true;
    void appendTree(const String &path, const String &prefix, bool last, bool top);
    void removeBranch(const String &path, bool includeRoot);
};

}
