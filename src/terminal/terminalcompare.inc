void compareDirectoryCommand(bool branch) {
  auto &s = active_.session;
  auto dest = ask("COMPARE with directory: ", destination());
  if (!dest)
    return;
  QString target = absolute(*dest);
  if (!QFileInfo(target).isDir() || QFileInfo(target).isSymLink()) {
    status_ = "Enter an existing real directory";
    return;
  }
  DirectoryCompareOptions options;
  while (true) {
    QStringList choices{
        "Run comparison",
        QString("Identical: %1")
            .arg(QStringList{"off", "size", "date",
                             "size and date"}[options.identical]),
        QString("Unique: %1").arg(options.unique ? "yes" : "no"),
        QString("Newer: %1").arg(options.newer ? "yes" : "no"),
        QString("Older: %1").arg(options.older ? "yes" : "no"),
        QString("Size: %1")
            .arg(QStringList{"off", "smaller", "larger",
                             "different"}[options.size]),
        QString("Binary: %1")
            .arg(QStringList{"off", "equal", "different"}[options.binary]),
        QString("Case sensitive: %1").arg(options.caseSensitive ? "yes" : "no"),
        "Reset"};
    int pick = choose("COMPARE options", choices);
    if (pick < 0)
      return;
    if (pick == 0)
      break;
    if (pick == 1)
      options.identical = (options.identical + 1) % 4;
    else if (pick == 2)
      options.unique = !options.unique;
    else if (pick == 3)
      options.newer = !options.newer;
    else if (pick == 4)
      options.older = !options.older;
    else if (pick == 5)
      options.size = (options.size + 1) % 4;
    else if (pick == 6)
      options.binary = (options.binary + 1) % 3;
    else if (pick == 7)
      options.caseSensitive = !options.caseSensitive;
    else
      options = {};
  }
  QVector<FileEntry> files;
  for (const auto &dir : s.directories())
    if ((branch ? isWithin(dir.path, s.directory) : dir.path == s.directory) &&
        dir.loaded) {
      if (!dir.error.isEmpty()) {
        status_ = dir.error;
        return;
      }
      for (const auto &file : dir.files)
        if (s.filespec.matches(file))
          files << file;
    }
  if (files.isEmpty()) {
    status_ = "No matching logged files; use L or * first";
    return;
  }
  const auto source = s.directory;
  auto result = task("Comparing directories", [=](const Cancellation &cancel) {
    auto scanned = scanDirectories(target, branch, cancel);
    DirectoryCompareResult result;
    QVector<FileEntry> targetFiles;
    for (const auto &dir : scanned.directories) {
      if (!dir.error.isEmpty())
        result.errors << dir.path + ": " + dir.error;
      targetFiles += dir.files;
    }
    if (scanned.cancelled) {
      result.cancelled = true;
      return result;
    }
    if (!result.errors.isEmpty())
      return result;
    return compareDirectories(files, source, targetFiles, target, options,
                              cancel);
  });
  if (!result.cancelled && result.errors.isEmpty()) {
    for (const auto &file : files) {
      if (result.matches.contains(file.path))
        s.tags.insert(file.path);
      else
        s.tags.remove(file.path);
    }
    s.rebuild();
  }
  status_ = QString("Compare: %1 matches; %2 errors")
                .arg(result.matches.size())
                .arg(result.errors.size());
  if (!result.errors.isEmpty())
    status_ += "; " + result.errors.first();
}
void filterCommand() {
  int pick = choose("File comparison filter",
                    {"Duplicate", "Unique", "Same size", "Same content",
                     "Identical dates", "Newest", "Oldest", "Clear"});
  if (pick < 0)
    return;
  auto &s = active_.session;
  if (pick == 7) {
    s.clearCompareFilter();
    return;
  }
  bool caseSensitive = true, includeEmpty = false;
  while (true) {
    int option =
        choose("FILTER options",
               {"Run filter",
                QString("Case sensitive: %1").arg(caseSensitive ? "yes" : "no"),
                QString("Include empty: %1").arg(includeEmpty ? "yes" : "no")});
    if (option < 0)
      return;
    if (option == 0)
      break;
    if (option == 1)
      caseSensitive = !caseSensitive;
    else
      includeEmpty = !includeEmpty;
  }
  const auto files = s.scopeFiles();
  auto result = task("Filtering files", [=](const Cancellation &cancel) {
    return filterDuplicates(files, CompareFilter(pick), caseSensitive,
                            includeEmpty, cancel);
  });
  if (!result.cancelled)
    s.setCompareFilter(result.matches,
                       pick == 2 || pick == 3 ? SortKey::Size : SortKey::Name);
  status_ = QString("Filter: %1 files; %2 errors")
                .arg(result.matches.size())
                .arg(result.errors.size());
}
CompareResult comparison_;
CompareOptions compareOptions_;
QString compareFirst_, compareSecond_;
int compareTop_ = 0;
bool compareHex_ = false, compareSplit_ = true, compareLineNumbers_ = true,
     compareByteDetail_ = true;
bool comparing_ = false;
void compareCommand(bool tagged) {
  auto files = targets(tagged);
  if (files.isEmpty()) {
    status_ = "No files selected";
    return;
  }
  compareFirst_ = files.first().path;
  if (tagged && files.size() == 2)
    compareSecond_ = files.last().path;
  else {
    auto dest =
        ask("COMPARE file with: ", other_ && other_->session.currentFile()
                                       ? other_->session.currentFile()->path
                                       : destination());
    if (!dest)
      return;
    compareSecond_ = absolute(*dest);
  }
  compareOptions_ = {};
  recompare();
  if (!comparison_.error.isEmpty()) {
    status_ = comparison_.error;
    return;
  }
  compareTop_ = 0;
  compareHex_ = !comparison_.textError.isEmpty();
  comparing_ = true;
  while (comparing_ && !stopped) {
    draw();
    int k = nextKey();
    if (k < 0)
      continue;
    const int h = compareSplit_ ? (rows_ - 5) / 2 : rows_ - 5;
    int count =
        compareHex_
            ? std::max(comparison_.firstBytes.size(),
                       comparison_.secondBytes.size()) /
                      bytesPerRow(compareSplit_ ? columns_ : columns_ / 2) +
                  1
            : comparison_.rows.size();
    if (k == KEY_F(1)) {
      showHelp(HelpTopic::Compare);
      continue;
    }
    if (k == 27 || k == 10 || k == 'q')
      comparing_ = false;
    else if (k == 'h') {
      compareHex_ = !compareHex_;
      compareTop_ = 0;
    } else if (k == 's')
      compareSplit_ = !compareSplit_;
    else if (k == 'd')
      compareByteDetail_ = !compareByteDetail_;
    else if (k == 'l')
      compareLineNumbers_ = !compareLineNumbers_;
    else if (k == 'c' || k == 'w' || k == 'e') {
      if (k == 'c')
        compareOptions_.caseSensitive = !compareOptions_.caseSensitive;
      if (k == 'w')
        compareOptions_.compressWhitespace =
            !compareOptions_.compressWhitespace;
      if (k == 'e')
        compareOptions_.suppressEmpty = !compareOptions_.suppressEmpty;
      recompare();
    } else if (k == KEY_UP)
      --compareTop_;
    else if (k == KEY_DOWN)
      ++compareTop_;
    else if (k == KEY_PPAGE)
      compareTop_ -= h;
    else if (k == KEY_NPAGE)
      compareTop_ += h;
    else if (k == KEY_HOME)
      compareTop_ = 0;
    else if (k == KEY_END)
      compareTop_ = count - h;
    else if (k == ' ' || k == '+' || k == '-') {
      const auto changes =
          compareHex_ ? comparison_.binaryBlocks : comparison_.changes;
      int target = -1;
      if (k == '-') {
        for (int change : changes) {
          int row = compareHex_
                        ? change / bytesPerRow(compareSplit_ ? columns_
                                                             : columns_ / 2)
                        : change;
          if (row < compareTop_)
            target = row;
        }
      } else {
        for (int change : changes) {
          int row = compareHex_
                        ? change / bytesPerRow(compareSplit_ ? columns_
                                                             : columns_ / 2)
                        : change;
          if (row > compareTop_) {
            target = row;
            break;
          }
        }
      }
      if (target >= 0)
        compareTop_ = target;
      else
        status_ = "No more differences";
    }
    compareTop_ = std::clamp(compareTop_, 0, std::max(0, count - 1));
  }
  comparing_ = false;
}
void recompare() {
  auto a = compareFirst_, b = compareSecond_;
  auto options = compareOptions_;
  comparison_ = task("Comparing files", [=](const Cancellation &cancel) {
    return compareFiles(a, b, cancel, options);
  });
}
void drawComparison() {
  int h = compareSplit_ ? (rows_ - 5) / 2 : rows_ - 5;
  int w = compareSplit_ ? columns_ : columns_ / 2;
  for (int side = 0; side < 2; ++side) {
    int x = compareSplit_ ? 0 : side * w,
        y = compareSplit_ ? side * (h + 1) : 0;
    const auto &bytes = side ? comparison_.secondBytes : comparison_.firstBytes;
    const auto &other = side ? comparison_.firstBytes : comparison_.secondBytes;
    text(x, y, side ? compareSecond_ : compareFirst_, 1, w);
    for (int r = 0; r < h; ++r) {
      int at = compareTop_ + r;
      if (compareHex_) {
        int start = at * bytesPerRow(w);
        if (start >= std::max(bytes.size(), other.size()))
          continue;
        text(x, y + r + 1, QString("%1 ").arg(start, 8, 16, QChar('0')), 1, 9);
        bool rowDiffers = bytes.mid(start, bytesPerRow(w)) !=
                          other.mid(start, bytesPerRow(w));
        for (int i = 0; i < bytesPerRow(w) &&
                        start + i < std::max(bytes.size(), other.size());
             ++i) {
          bool present = start + i < bytes.size();
          bool differs = compareByteDetail_
                             ? !present || start + i >= other.size() ||
                                   bytes[start + i] != other[start + i]
                             : rowDiffers;
          text(x + 10 + i * 3, y + r + 1,
               present ? QString("%1 ")
                             .arg(quint8(bytes[start + i]), 2, 16, QChar('0'))
                             .toUpper()
                       : QString("-- "),
               differs ? 14 : 2, 3);
          if (w >= 11 + bytesPerRow(w) * 4)
            text(x + 11 + bytesPerRow(w) * 3 + i, y + r + 1,
                 present ? dosGlyph(quint8(bytes[start + i])) : QString("-"),
                 differs ? 14 : 2, 1);
        }
      } else if (at < comparison_.rows.size()) {
        const auto &row = comparison_.rows[at];
        int index = side ? row.second : row.first;
        const auto &lines = side ? comparison_.second : comparison_.first;
        if (index >= 0 && index < lines.size())
          text(x, y + r + 1,
               (compareLineNumbers_
                    ? QString::number(index + 1).rightJustified(5) + " "
                    : QString()) +
                   lines[index],
               row.changed ? 14 : 2, w);
      }
    }
  }
  commands(0, rows_ - 3,
           "[Space/+/-] Difference [H] Hex [S] Split [D] Row/Byte [L] Line "
           "numbers [C] Case "
           "[W] Spaces [E] Empty [Esc] Return");
  text(
      0, rows_ - 2,
      QString("%1 changes  %2 different bytes  Case: %3  Spaces: %4  Empty: %5")
          .arg(comparison_.changes.size())
          .arg(comparison_.differentBytes)
          .arg(compareOptions_.caseSensitive)
          .arg(compareOptions_.compressWhitespace)
          .arg(compareOptions_.suppressEmpty));
}
