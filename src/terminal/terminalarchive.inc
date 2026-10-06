ArchiveCatalog archiveCatalog_;
QString archivePath_, archiveDirectory_, archivePassword_;
QSet<int> archiveTags_;
QVector<int> archiveVisible_;
int archiveIndex_ = 0, archiveTop_ = 0;
bool archiveBranch_ = false;
Filespec archiveSpec_;
void rebuildArchive() {
  archiveVisible_.clear();
  for (int i = 0; i < archiveCatalog_.entries.size(); ++i) {
    const auto &entry = archiveCatalog_.entries[i];
    if (!entry.safe || !entry.path.startsWith(archiveDirectory_))
      continue;
    QString relative = entry.path.mid(archiveDirectory_.size());
    if (relative.isEmpty())
      continue;
    if (!archiveBranch_ &&
        relative.remove(QRegularExpression("/$")).contains('/'))
      continue;
    if (entry.directory ||
        archiveSpec_.matches(QFileInfo(entry.path).fileName()))
      archiveVisible_.append(i);
  }
  archiveIndex_ = std::clamp(archiveIndex_, 0,
                             std::max(0, int(archiveVisible_.size()) - 1));
}
void drawArchive() {
  text(1, 0, archivePath_ + " / " + archiveDirectory_, 1, columns_ - 2);
  text(1, 1,
       QString("<%1: %2> %3")
           .arg(archiveBranch_ ? "branch" : "directory", archiveSpec_.text(),
                archiveCatalog_.format),
       2);
  const int h = rows_ - 6;
  archiveTop_ = std::clamp(archiveTop_, std::max(0, archiveIndex_ - h + 1),
                           archiveIndex_);
  for (int r = 0, i = archiveTop_; r < h && i < archiveVisible_.size();
       ++r, ++i) {
    const auto &entry = archiveCatalog_.entries[archiveVisible_[i]];
    int pair = i == archiveIndex_ ? 3
               : entry.directory  ? 1
                                  : fileInk(entry.path);
    if (i == archiveIndex_)
      fill(1, r + 2, columns_ - 2, 3);
    text(1, r + 2,
         (archiveTags_.contains(entry.index) ? "♦" : " ") +
             entry.path.mid(archiveDirectory_.size()),
         pair, columns_ - 26);
    text(columns_ - 24, r + 2,
         QString::number(entry.size).rightJustified(12) +
             (entry.directory ? " DIR" : ""),
         pair, 23);
  }
  text(1, rows_ - 4,
       QString("%1/%2 Tagged %3")
           .arg(archiveVisible_.isEmpty() ? 0 : archiveIndex_ + 1)
           .arg(archiveVisible_.size())
           .arg(archiveTags_.size()),
       1);
  commands(
      0, rows_ - 3,
      menu_ == 1 ? "CTRL ARCHIVE: [C/E] Extract tagged [S] Search [T/U] All"
      : menu_ == 2
          ? "ALT ARCHIVE: [C/E] Extract tagged with paths"
          : "[Enter/V] View [Backspace] Parent [B]ranch [F]ilespec [T/U] Tag "
            "[Ctrl+T/U] All [C/E] Extract");
  commands(0, rows_ - 2,
           "[Ctrl/Alt+C/E] Extract tagged [Ctrl+S] Search [F9] Password [F1] "
           "Help [Esc] Return");
}
void synthesizeArchiveDirectories() {
  QSet<QString> directories;
  for (const auto &entry : archiveCatalog_.entries)
    if (entry.directory)
      directories.insert(entry.path.endsWith('/') ? entry.path
                                                  : entry.path + "/");
  QVector<ArchiveEntry> parents;
  int synthetic = -1;
  for (const auto &entry : archiveCatalog_.entries) {
    QString directory = QFileInfo(entry.path).path();
    while (directory != "." && !directory.isEmpty()) {
      QString name = directory + "/";
      if (!directories.contains(name)) {
        directories.insert(name);
        ArchiveEntry dir;
        dir.index = synthetic--;
        dir.path = name;
        dir.directory = dir.safe = true;
        parents.append(dir);
      }
      directory = QFileInfo(directory).path();
    }
  }
  archiveCatalog_.entries = parents + archiveCatalog_.entries;
}
void archiveOpen(const QString &path, bool listing = false) {
  auto catalog =
      task("Reading archive", [this, path](const Cancellation &cancel) {
        return listArchive(path, cancel, archivePassword_);
      });
  if (!catalog.error.isEmpty()) {
    status_ = catalog.error;
    auto password = ask("Archive password: ");
    if (!password)
      return;
    archivePassword_ = *password;
    catalog = task("Reading archive", [this, path](const Cancellation &cancel) {
      return listArchive(path, cancel, archivePassword_);
    });
  }
  if (!catalog.error.isEmpty() || catalog.cancelled) {
    status_ = catalog.error;
    return;
  }
  archivePath_ = path;
  archiveCatalog_ = catalog;
  synthesizeArchiveDirectories();
  archiveDirectory_.clear();
  archiveTags_.clear();
  archiveIndex_ = archiveTop_ = 0;
  archiveBranch_ = false;
  archiveSpec_.set("*.*");
  if (listing) {
    QString text;
    for (const auto &entry : catalog.entries)
      text += QString("%1 %2 %3\n")
                  .arg(entry.directory ? "DIR" : "   ")
                  .arg(entry.size, 12)
                  .arg(entry.path);
    setMemoryDocument(text.toUtf8(), path + " [archive listing]");
    return;
  }
  inArchive_ = true;
  rebuildArchive();
  menu_ = 0;
  while (inArchive_ && !stopped) {
    draw();
    int k = nextKey();
    if (k < 0)
      continue;
    if (k == KEY_F(4)) {
      menu_ = (menu_ + 1) % 3;
      continue;
    }
    if (k == 27 && menu_) {
      menu_ = 0;
      continue;
    }
    int menu = std::exchange(menu_, 0);
    if (menu == 1) {
      if (k == 's')
        k = 19;
      else if (k == 't')
        k = 20;
      else if (k == 'u')
        k = 21;
    }
    if (k == 27) {
      inArchive_ = false;
      break;
    }
    if (k == 0x3000 + 'c' || k == 0x3000 + 'e') {
      archiveExtract(true, true);
      continue;
    }
    if (k == KEY_UP)
      --archiveIndex_;
    else if (k == KEY_DOWN || k == ' ')
      ++archiveIndex_;
    else if (k == KEY_PPAGE)
      archiveIndex_ -= rows_ - 6;
    else if (k == KEY_NPAGE)
      archiveIndex_ += rows_ - 6;
    else if (k == KEY_HOME)
      archiveIndex_ = 0;
    else if (k == KEY_END)
      archiveIndex_ = archiveVisible_.size() - 1;
    else if (k == KEY_BACKSPACE || k == 127 || k == 8) {
      if (!archiveDirectory_.isEmpty()) {
        auto dir = archiveDirectory_;
        dir.chop(1);
        int at = dir.lastIndexOf('/');
        archiveDirectory_ = at < 0 ? QString() : dir.left(at + 1);
        archiveIndex_ = 0;
        rebuildArchive();
      }
    } else if (k == 'b' || k == 'B') {
      archiveBranch_ = !archiveBranch_;
      rebuildArchive();
    } else if (k == 'f' || k == 'F') {
      auto value = ask("FILESPEC: ", archiveSpec_.text(), "filespec");
      if (value) {
        QString error;
        if (!archiveSpec_.set(*value, &error))
          status_ = error;
        rebuildArchive();
      }
    } else if (k == 20 || k == 21) {
      for (int i : archiveVisible_)
        if (!archiveCatalog_.entries[i].directory) {
          if (k == 20)
            archiveTags_.insert(archiveCatalog_.entries[i].index);
          else
            archiveTags_.remove(archiveCatalog_.entries[i].index);
        }
    } else if (k == KEY_F(3)) {
      auto catalog =
          task("Reloading archive", [this](const Cancellation &cancel) {
            return listArchive(archivePath_, cancel, archivePassword_);
          });
      if (catalog.error.isEmpty() && !catalog.cancelled) {
        archiveCatalog_ = catalog;
        synthesizeArchiveDirectories();
        rebuildArchive();
      } else
        status_ = catalog.error;
    } else if (k == KEY_F(9)) {
      auto password = ask("Archive password: ", archivePassword_);
      if (password)
        archivePassword_ = *password;
    } else if (k == KEY_F(1)) {
      helpDocument_.open(HelpTopic::Archives);
      help_ = true;
      while (help_ && !stopped) {
        draw();
        int value = nextKey();
        if (value >= 0)
          helpKey(value);
      }
    } else if (k == 19) {
      archiveSearch();
    } else if (k == 'c' || k == 'e' || k == 3 || k == 5) {
      archiveExtract(k == 3 || k == 5 || menu != 0, menu == 2);
    } else if (k == 27)
      inArchive_ = false;
    else if (!archiveVisible_.isEmpty()) {
      auto entry = archiveCatalog_.entries[archiveVisible_[std::clamp(
          archiveIndex_, 0, int(archiveVisible_.size()) - 1)]];
      if (k == 't' || k == 'T') {
        archiveTags_.insert(entry.index);
        ++archiveIndex_;
      } else if (k == 'u' || k == 'U') {
        archiveTags_.remove(entry.index);
        ++archiveIndex_;
      } else if (k == 10 || k == 13 || k == KEY_ENTER || k == 'v' || k == 'V') {
        if (entry.directory) {
          archiveDirectory_ = entry.path;
          if (!archiveDirectory_.endsWith('/'))
            archiveDirectory_ += '/';
          archiveBranch_ = false;
          archiveIndex_ = 0;
          rebuildArchive();
        } else {
          auto bytes =
              task("Reading " + entry.path,
                   [this, entry](const Cancellation &cancel) {
                     return readArchiveMember(archivePath_, entry.index, cancel,
                                              archivePassword_);
                   });
          if (bytes.error.isEmpty()) {
            setMemoryDocument(bytes.bytes, archivePath_ + " / " + entry.path);
            while (viewer_ && !stopped) {
              draw();
              int value = nextKey();
              if (value >= 0)
                extendedViewerKey(value, false, value < KEY_MIN);
            }
          } else
            status_ = bytes.error;
        }
      }
    }
    archiveIndex_ = std::clamp(archiveIndex_, 0,
                               std::max(0, int(archiveVisible_.size()) - 1));
  }
  inArchive_ = false;
}
void archiveExtract(bool tagged, bool paths) {
  QSet<int> entries = tagged ? archiveTags_ : QSet<int>{};
  if (!tagged && !archiveVisible_.isEmpty())
    entries.insert(
        archiveCatalog_.entries[archiveVisible_[archiveIndex_]].index);
  if (entries.isEmpty()) {
    status_ = "No archive members selected";
    return;
  }
  QSet<int> expanded;
  for (const auto &entry : archiveCatalog_.entries)
    if (entries.contains(entry.index)) {
      if (entry.directory) {
        for (const auto &child : archiveCatalog_.entries)
          if (child.safe && !child.directory &&
              child.path.startsWith(entry.path))
            expanded.insert(child.index);
      } else
        expanded.insert(entry.index);
    }
  entries = expanded;
  bool replace = false;
  auto dest = ask(
      "EXTRACT to: ", destination(), {},
      [&] {
        return QString("[F2] Preserve paths (%1) [F4] Replace (%2) [Enter] "
                       "Extract [Esc] Cancel")
            .arg(paths ? "yes" : "no", replace ? "yes" : "no");
      },
      [&](int k, QString &) {
        if (k == KEY_F(2))
          paths = !paths;
        else if (k == KEY_F(4))
          replace = !replace;
        else
          return false;
        return true;
      });
  if (!dest)
    return;
  const auto target = absolute(*dest);
  auto result =
      task("Extracting archive",
           [this, entries, target, paths, replace](const Cancellation &cancel) {
             return extractArchive(archivePath_, entries, target, paths,
                                   replace, cancel, archivePassword_);
           });
  QSet<QString> refresh{target};
  auto collect = [&](const Session &session) {
    for (auto it = session.directories().cbegin();
         it != session.directories().cend(); ++it)
      if (it->loaded && isWithin(it.key(), target))
        refresh.insert(it.key());
  };
  collect(active_.session);
  if (other_)
    collect(other_->session);
  for (const auto &pane : roots_)
    collect(pane.session);
  refreshDirectories(refresh);
  status_ = QString("Extract: %1 completed; %2 failed; %3")
                .arg(result.completed)
                .arg(result.failed)
                .arg(result.error);
}
void archiveSearch() {
  SearchOptions options = rememberedSearch_;
  if (!searchPrompt(options))
    return;
  rememberedSearch_ = options;
  QSet<int> hits;
  int failed = 0, count = 0;
  auto candidates = archiveTags_;
  if (candidates.isEmpty())
    for (int i : archiveVisible_)
      if (!archiveCatalog_.entries[i].directory)
        candidates.insert(archiveCatalog_.entries[i].index);
  hits = candidates;
  for (int i : archiveVisible_) {
    const auto &entry = archiveCatalog_.entries[i];
    if (!candidates.contains(entry.index))
      continue;
    archiveIndex_ = archiveVisible_.indexOf(i);
    auto bytes =
        task(QString("Searching %1  Hits: %2").arg(entry.path).arg(count),
             [this, entry](const Cancellation &cancel) {
               return readArchiveMember(archivePath_, entry.index, cancel,
                                        archivePassword_);
             });
    if (bytes.cancelled)
      break;
    if (!bytes.error.isEmpty()) {
      ++failed;
      continue;
    }
    ViewDocument doc = decodeBytes(bytes.bytes);
    if (findInDocument(doc, options, 0, false).first >= 0)
      ++count;
    else if (!viewerSearchError_.isEmpty())
      ++failed;
    else
      hits.remove(entry.index);
  }
  archiveTags_ = hits;
  status_ =
      QString("Archive search: %1 hits; %2 errors").arg(count).arg(failed);
}
void createArchiveCommand(bool tagged) {
  const auto files = targets(tagged);
  if (files.isEmpty()) {
    status_ = "No files selected";
    return;
  }
  ArchiveFormat format = ArchiveFormat::Zip;
  bool paths = true, replace = false;
  auto output = ask(
      "CREATE archive as: ",
      QDir(destination())
          .filePath(QFileInfo(active_.session.directory).fileName() + ".zip"),
      {},
      [&] {
        return QString("[F3] Format (%1) [F2] Preserve paths (%2) [F4] Replace "
                       "(%3) [Enter] Create [Esc] Cancel")
            .arg(archiveFormatName(format), paths ? "yes" : "no",
                 replace ? "yes" : "no");
      },
      [&](int k, QString &name) {
        if (k == KEY_F(2))
          paths = !paths;
        else if (k == KEY_F(4))
          replace = !replace;
        else if (k == KEY_F(3)) {
          QStringList formats;
          for (int i = 0; i < 9; ++i)
            formats << archiveFormatName(ArchiveFormat(i));
          int selected = choose("Archive format", formats, int(format));
          if (selected >= 0) {
            const auto old = archiveFormatSuffix(format);
            format = ArchiveFormat(selected);
            if (name.endsWith(old, Qt::CaseInsensitive))
              name.chop(old.size());
            name += archiveFormatSuffix(format);
          }
        } else
          return false;
        return true;
      });
  if (!output)
    return;
  ArchiveFormat inferred;
  if (archiveFormatFromPath(*output, inferred))
    format = inferred;
  if (archiveStreamFormat(format) && files.size() != 1) {
    status_ = "Stream formats require exactly one file; select "
              "TAR.GZ/TAR.BZ2/TAR.XZ for multiple files";
    return;
  }
  const auto target = absolute(*output), base = active_.session.directory;
  QStringList sources;
  for (const auto &file : files)
    sources << file.path;
  if (replace && QFileInfo::exists(target) &&
      !confirm("Replace archive " + target + "?"))
    return;
  auto result = task("Creating " + archiveFormatName(format),
                     [=](const Cancellation &cancel) {
                       return createArchive(sources, base, target, format,
                                            paths, replace, cancel);
                     });
  refreshDirectories({QFileInfo(target).absolutePath()});
  status_ = QString("Archive: %1 completed; %2")
                .arg(result.completed)
                .arg(result.error);
}
void openSelected() {
  if (!active_.session.currentFile())
    return;
  const auto path = active_.session.currentFile()->path;
  if (archiveName(path))
    archiveOpen(path);
  else
    readDocument(path);
}
