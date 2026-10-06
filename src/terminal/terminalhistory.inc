struct History {
  QStringList entries;
  QMap<QString, QString> marks;
};
QMap<QString, History> histories_;
QMap<QString, Pane> roots_;
std::optional<Pane> globalReturn_;
bool info_ = false, inArchive_ = false, avail_ = false;
QStringList viewPaths_;
int viewSequenceIndex_ = 0, extensionWidth_ = 9, statisticsMode_ = 0;
SearchOptions rememberedSearch_;
QString historyDirectory() const {
  const auto override = qEnvironmentVariable("LTREE_HISTORY_FILE");
  return override.isEmpty() ? QStandardPaths::writableLocation(
                                  QStandardPaths::GenericConfigLocation) +
                                  "/ltreec"
                            : QFileInfo(override).absolutePath();
}
QString historyPath(const QString &key) const {
  const auto override = qEnvironmentVariable("LTREE_HISTORY_FILE");
  return key == "search" && !override.isEmpty()
             ? override
             : historyDirectory() + "/" + key + "-history.json";
}
void readHistory(const QString &key) {
  QFile file(historyPath(key));
  if (!file.open(QIODevice::ReadOnly))
    return;
  const auto object = QJsonDocument::fromJson(file.readAll()).object();
  if (object.value("version").toInt() != 1)
    return;
  History value;
  for (const auto &entry : object.value(key).toArray()) {
    const auto text = entry.isString()
                          ? entry.toString()
                          : entry.toObject().value("text").toString();
    if (text.isEmpty() || text.contains(QChar(0)) || text.contains('\n') ||
        text.size() > 4096 || value.entries.contains(text))
      continue;
    value.entries.append(text);
    value.marks[text] = entry.toObject().value("mark").toString();
  }
  histories_[key] = value;
}
void saveHistory(const QString &key) {
  QDir().mkpath(historyDirectory());
  QJsonArray entries;
  for (const auto &entry : histories_[key].entries)
    entries.append(QJsonObject{{"text", entry},
                               {"mark", histories_[key].marks.value(entry)}});
  QSaveFile file(historyPath(key));
  if (!file.open(QIODevice::WriteOnly)) {
    status_ = file.errorString();
    return;
  }
  file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  const auto bytes =
      QJsonDocument(QJsonObject{{"version", 1}, {key, entries}}).toJson();
  if (file.write(bytes) != bytes.size() || !file.commit())
    status_ = file.errorString();
}
void remember(const QString &key, const QString &value) {
  if (value.isEmpty())
    return;
  auto &history = histories_[key];
  history.entries.removeAll(value);
  history.entries.prepend(value);
  int count = 0;
  for (int i = 0; i < history.entries.size();) {
    if (history.marks.value(history.entries[i]).isEmpty() && ++count > 64)
      history.entries.removeAt(i);
    else
      ++i;
  }
  saveHistory(key);
  saveViewerHistory();
}
void initializeTerminalHistories() {
  for (const auto &key :
       QStringList{"search", "filespec", "stamp", "graft", "gather"})
    readHistory(key);
  QFile viewer(historyDirectory() + "/viewer-history.json");
  if (viewer.open(QIODevice::ReadOnly)) {
    const auto object = QJsonDocument::fromJson(viewer.readAll()).object();
    for (const auto &key : QStringList{"search", "gather"})
      for (const auto &entry : object.value(key).toArray()) {
        QString text = entry.toString();
        if (!text.isEmpty() && !histories_[key].entries.contains(text))
          histories_[key].entries.append(text);
      }
  }
  filespecHistory_ = histories_["filespec"].entries;
  stampHistory_ = histories_["stamp"].entries;
}
void saveTerminalHistories() {

  for (const auto &key :
       QStringList{"search", "filespec", "stamp", "graft", "gather"})
    saveHistory(key);
  saveViewerHistory();
}
int nextKey(bool *literal = nullptr) {
  const auto input = keyboard_.read();
  QCoreApplication::processEvents();
  if (stopped) return 27;
  if (!input) return -1;
  if (literal) *literal = input->literal;
  if (input->value == KEY_RESIZE) { getmaxyx(stdscr,rows_,columns_);return -1; }
  int value = input->value;
  if (input->control && value >= 'a' && value <= 'z') value -= 'a'-1;
  return input->alt ? 0x3000 + value : value;
}
std::optional<QString> historyPopup(const QString &key, const QString &draft) {
  auto &history = histories_[key];
  int index = std::max(0, int(history.entries.size()) - 1), filter = 0,
      sort = 0;
  QString notice;
  while (!stopped) {
    draw();
    const int x = std::max(1, columns_ / 6), w = columns_ - x - 1,
              h = std::max(1, rows_ - 8);
    QStringList items;
    for (auto it = history.entries.crbegin(); it != history.entries.crend();
         ++it) {
      const auto mark = history.marks.value(*it);
      if (filter == 1 && mark.isEmpty())
        continue;
      if (filter == 2 && (mark.isEmpty() || mark == "*"))
        continue;
      if (filter == 3 && !mark.isEmpty())
        continue;
      items.append(*it);
    }
    if (sort)
      std::stable_sort(items.begin(), items.end(),
                       [&](const QString &a, const QString &b) {
                         auto first = sort <= 2 ? history.marks.value(a) : a,
                              second = sort <= 2 ? history.marks.value(b) : b;
                         int order = first.compare(second, Qt::CaseInsensitive);
                         return sort % 2 ? order < 0 : order > 0;
                       });
    index = std::clamp(index, 0, std::max(0, int(items.size()) - 1));
    int top = std::max(0, index - h + 1);
    fill(x, 2, w, 12);
    text(x, 2, key.toUpper() + " HISTORY", 12, w);
    for (int i = 0; i < h; ++i) {
      fill(x, 3 + i, w, i + top == index ? 3 : 12);
      if (i + top < items.size())
        text(x, 3 + i,
             history.marks.value(items[i + top]).leftJustified(2) +
                 items[i + top],
             i + top == index ? 3 : 12, w);
    }
    fill(x, rows_ - 4, w, 1);
    commands(
        x, rows_ - 4,
        "[↑↓/PgUp/PgDn/Home/End] Navigate [←→] Filter [Del] Delete [Ins] Mark");
    fill(x, rows_ - 3, w, 1);
    commands(x, rows_ - 3,
             "[Enter] Retrieve [|] Append [F2] Sort [F3] Save [F4] Load [Esc] "
             "Cancel");
    fill(x, rows_ - 2, w, 1);
    text(x, rows_ - 2,
         notice.isEmpty() ? QString("Displayed items: %1  Filter: %2")
                                .arg(items.size())
                                .arg(filter)
                          : notice);
    wrefresh(stdscr);
    int k = nextKey();
    if (k < 0)
      continue;
    if (k == 27)
      return {};
    if (k == KEY_UP)
      --index;
    else if (k == KEY_DOWN)
      ++index;
    else if (k == KEY_PPAGE)
      index -= h;
    else if (k == KEY_NPAGE)
      index += h;
    else if (k == KEY_HOME)
      index = 0;
    else if (k == KEY_END)
      index = items.size() - 1;
    else if (k == KEY_LEFT || k == KEY_RIGHT) {
      filter = (filter + (k == KEY_RIGHT ? 1 : 3)) % 4;
      index = 0;
    } else if (k == KEY_F(3)) {
      saveHistory(key);
      notice = "History saved";
    } else if (k == KEY_F(4)) {
      readHistory(key);
      notice = "History loaded";
    } else if (k == KEY_F(2) || k == AltLeft || k == AltRight ||
               k == 0x3000 + KEY_LEFT || k == 0x3000 + KEY_RIGHT) {
      sort = (sort + (k == AltLeft || k == 0x3000 + KEY_LEFT ? 4 : 1)) % 5;
      index = 0;
    } else if (!items.isEmpty()) {
      const auto item = items[index];
      if (k == KEY_DC) {
        if (!history.marks.value(item).isEmpty()) {
          notice = "Unmark the item before deleting it";
          continue;
        }
        history.entries.removeAll(item);
        history.marks.remove(item);
        saveHistory(key);
      } else if (k == KEY_IC) {
        history.marks[item] = history.marks.value(item).isEmpty() ? "*" : "";
        saveHistory(key);
      } else if (k == 10 || k == 13 || k == KEY_ENTER)
        return item;
      else if (k == '|' || k == '+')
        return draft + item;
      else if ((k >= 32 && k < 127) || (k > 0 && k < 27)) {
        if (k > 0 && k < 27)
          k += 'A' - 1;
        const QString mark = QString(QChar(k)).toUpper();
        for (const auto &entry : items)
          if (history.marks.value(entry) == mark)
            return entry;
        history.marks[item] = mark;
        saveHistory(key);
      }
    }
  }
  return {};
}
std::optional<QString>
ask(const QString &label, QString input = {}, const QString &history = {},
    const std::function<QString()> &options = {},
    const std::function<bool(int, QString &)> &optionKey = {}) {
  int cursor = input.size();
  bool selected = !input.isEmpty();
  while (!stopped) {
    draw();
    for (int y = rows_ - 3; y < rows_; ++y)
      fill(0, y, columns_, 1);
    text(0, rows_ - 3, label + input, 2);
    commands(0, rows_ - 2,
             options ? options()
                     : "[Enter] OK [Esc] Cancel [↑↓] History [F3] Last");
    text(0, rows_ - 1, status_, 1);
    const int start = std::max(0, cursor - (columns_ - int(label.size()) - 2));
    fill(0, rows_ - 3, columns_, 2);
    text(0, rows_ - 3, label + input.mid(start), 2);
    const int cx =
        std::clamp(int(label.size()) + cursor - start, 0, columns_ - 1);
    wmove(stdscr, rows_ - 3, cx);
    curs_set(2);
    wrefresh(stdscr);
    bool literal;
    int k = nextKey(&literal);
    if (k < 0)
      continue;
    if (k == 27 || k == 3) {
      curs_set(0);
      return {};
    }
    if (k == 10 || k == 13 || k == KEY_ENTER) {
      curs_set(0);
      if (!history.isEmpty())
        remember(history, input);
      return input;
    }
    if (optionKey && optionKey(k, input)) {
      cursor = input.size();
      selected = true;
      continue;
    }
    if (k == KEY_F(2) && (label.contains("to:", Qt::CaseInsensitive) ||
                          label.contains("with", Qt::CaseInsensitive) ||
                          label.contains("destination", Qt::CaseInsensitive))) {
      auto chosen = browseDirectory(input);
      if (chosen) {
        input = *chosen;
        cursor = input.size();
        selected = true;
      }
      continue;
    }
    if ((k == KEY_UP || k == KEY_DOWN) && !history.isEmpty()) {
      auto chosen = historyPopup(history, input);
      if (chosen) {
        input = *chosen;
        cursor = input.size();
        selected = true;
      }
      continue;
    }
    if (k == KEY_F(3) && !history.isEmpty() &&
        !histories_[history].entries.isEmpty()) {
      input = histories_[history].entries.first();
      cursor = input.size();
      selected = true;
      continue;
    }
    if (k == KEY_LEFT) {
      cursor = std::max(0, cursor - 1);
      selected = false;
    } else if (k == KEY_RIGHT) {
      cursor = std::min(int(input.size()), cursor + 1);
      selected = false;
    } else if (k == KEY_HOME || k == 1) {
      cursor = 0;
      selected = false;
    } else if (k == KEY_END || k == 5) {
      cursor = input.size();
      selected = false;
    } else if (k == 21) {
      input.clear();
      cursor = 0;
      selected = false;
    } else if (k == KEY_BACKSPACE || k == 127 || k == 8 || k == KEY_LEFT) {
      if (selected) {
        input.clear();
        cursor = 0;
      } else if (cursor)
        input.remove(--cursor, 1);
      selected = false;
    } else if (k == KEY_DC) {
      if (selected) {
        input.clear();
        cursor = 0;
      } else
        input.remove(cursor, 1);
      selected = false;
    } else if (literal && k >= 32 && k != 127 && input.size() < 4096) {
      if (selected) {
        input.clear();
        cursor = 0;
        selected = false;
      }
      const char32_t cp = k;
      const auto added = QString::fromUcs4(&cp, 1);
      input.insert(cursor, added);
      cursor += added.size();
    }
  }
  return {};
}
int choose(const QString &title, const QStringList &choices, int index = 0) {
  while (!stopped) {
    draw();
    int h = std::max(1, rows_ - 7);
    index = std::clamp(index, 0, std::max(0, int(choices.size()) - 1));
    int top = std::max(0, index - h + 1);
    fill(1, 2, columns_ - 2, 1);
    text(2, 2, title, 2);
    for (int i = 0; i < h; ++i) {
      fill(1, 3 + i, columns_ - 2, i + top == index ? 3 : 1);
      if (i + top < choices.size())
        text(2, 3 + i, choices[i + top], i + top == index ? 3 : 2,
             columns_ - 4);
    }
    fill(0, rows_ - 2, columns_, 1);
    commands(0, rows_ - 2, "[↑↓] Select [Enter] OK [Esc] Cancel");
    wrefresh(stdscr);
    int k = nextKey();
    if (k == 27)
      return -1;
    if (k == KEY_UP)
      --index;
    else if (k == KEY_DOWN)
      ++index;
    else if (k == KEY_PPAGE)
      index -= h;
    else if (k == KEY_NPAGE)
      index += h;
    else if (k == KEY_HOME)
      index = 0;
    else if (k == KEY_END)
      index = choices.size() - 1;
    else if (k == 10 || k == 13 || k == KEY_ENTER)
      return index;
  }
  return -1;
}
bool confirm(const QString &question) {
  while (!stopped) {
    draw();
    fill(0, rows_ - 2, columns_, 1);
    text(0, rows_ - 2, question, 2);
    fill(0, rows_ - 1, columns_, 1);
    commands(0, rows_ - 1, "[Y] Yes [N/Esc] No");
    wrefresh(stdscr);
    int k = nextKey();
    if (k == 'y' || k == 'Y')
      return true;
    if (k == 'n' || k == 'N' || k == 27 || k == 3)
      return false;
  }
  return false;
}
template <class Function> auto task(const QString &label, Function function) {
  cancel_ = std::make_shared<std::atomic_bool>(false);
  const auto token = cancel_;
  auto future =
      QtConcurrent::run([function, token] { return function(token); });
  while (!future.isFinished()) {
    draw();
    fill(0, rows_ - 1, columns_, 1);
    commands(0, rows_ - 1,
             QString("%1 %2 [Esc] Cancel")
                 .arg(QString("|/-\\")[spinner_++ % 4])
                 .arg(label));
    wrefresh(stdscr);
    int k = nextKey();
    if (k == 27 || k == 3 || stopped)
      token->store(true);
  }
  return future.result();
}
QVector<FileEntry> targets(bool tagged) const {
  QVector<FileEntry> files;
  auto &s = active_.session;
  if (tagged) {
    for (const auto &file : s.files())
      if (s.tags.contains(file.path))
        files.append(file);
  } else if (s.currentFile())
    files.append(*s.currentFile());
  return files;
}
QString destination() const {
  return other_ ? other_->session.directory : active_.session.directory;
}
QString absolute(const QString &path) const {
  QString p = path;
  if (p == "~")
    p = QDir::homePath();
  else if (p.startsWith("~/"))
    p = QDir::homePath() + p.mid(1);
  return QDir::cleanPath(QDir(active_.session.directory).absoluteFilePath(p));
}
void applyScan(const ScanResult &scan) {
  active_.session.apply(scan, true);
  if (other_)
    other_->session.apply(scan, true);
  for (auto it = roots_.begin(); it != roots_.end(); ++it)
    it->session.apply(scan, true);
  invalidateAutoview();
}
void refreshDirectories(const QSet<QString> &directories) {
  const auto result =
      task("Refreshing panels", [directories](const Cancellation &cancel) {
        ScanResult scan;
        for (const auto &path : directories)
          if (QFileInfo(path).isDir())
            scan.directories +=
                scanDirectories(path, false, cancel).directories;
        return scan;
      });
  applyScan(result);
}
int fileInk(const QString &name) const {
  const auto ext = QFileInfo(name).suffix().toUpper();
  if (QStringList{"COM", "EXE", "BAT", "BTM", "CMD", "VBS", "JS", "WSH", "SH"}
          .contains(ext))
    return 4;
  if (QStringList{"DLL", "DRV", "SYS", "SO"}.contains(ext))
    return 5;
  if (QStringList{"C", "H", "CPP", "HPP", "MAK", "DEF"}.contains(ext))
    return 6;
  if (QStringList{"TXT", "DAT", "HLP", "ASC", "LOG", "DOC"}.contains(ext))
    return 7;
  if (archiveName(name))
    return 8;
  if (QStringList{"INI", "INF", "CFG"}.contains(ext))
    return 9;
  if (QStringList{"TMP", "BAK"}.contains(ext))
    return 10;
  return 2;
}
