String shellQuote(String value) const {
  value.replace('\'', "'\\''");
  return "'" + value + "'";
}
bool extendedKey(int value, bool alt, bool control, bool literal) {
  auto &s = active_.session;
  bool tree = s.view == View::Tree;
  int letter = value >= 'A' && value <= 'Z' ? value + 32 : value;
  if (value == 0x2604 || value == 0x2605) {
    auto spell = tree ? s.treeSpell : s.fileSpell;
    if (value == 0x2604)
      spell.chop(1);
    else
      spell.clear();
    s.spell(spell);
    return true;
  }
  if (value == '"') {
    s.spell(tree ? s.treeSpell : s.fileSpell, true);
    return true;
  }
  if (value == 0x2601) {
    control = true;
    value = 13;
    letter = value;
  } else if (value == 0x2602) {
    control = true;
    value = 9;
    letter = value;
  } else if (value == 0x2603) {
    control = true;
    value = 'd';
    letter = value;
  }
  if (autoview_ &&
      (value == 0x220a || (literal && value >= 'A' && value <= 'Z'))) {
    forwardPreviewKey(value == 0x220a ? KEY_F(10) : value + 32, false, literal);
    return true;
  }
  if (literal && !alt && !control &&
      ((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))) {
    auto spell = tree ? s.treeSpell : s.fileSpell;
    const char32_t cp = value;
    spell += String::fromUcs4(&cp, 1);
    if (!s.spell(spell))
      status_ = "No match";
    return true;
  }
  if (control && letter == 'f') {
    const auto &history = histories_["filespec"].entries;
    if (!history.isEmpty()) {
      int at =
          history.first() == s.filespec.text() && history.size() > 1 ? 1 : 0;
      s.filespec.set(history[at]);
      s.rebuild();
    }
    return true;
  }
  if (value >= 0x2501 && value <= 0x250c) {
    alt = true;
    value = KEY_F(value - 0x2500);
    letter = value;
  }
  if (value >= 0x2101 && value <= 0x210c) {
    control = true;
    value = KEY_F(value - 0x2100);
    letter = value;
  }
  if (value == 0x2208) {
    int pick =
        choose("Split panels", {"Swap panels", "Statistics: none",
                                "Statistics: current", "Statistics: both"});
    if (pick == 0 && other_) {
      stopAutoview();
      std::swap(active_, *other_);
      side_ = 1 - side_;
    } else if (pick > 0)
      statisticsMode_ = pick - 1;
    return true;
  }
  if (value == 0x2401 || value == 0x2402 || value == 0x2403) {
    extensionWidth_ =
        value == 0x2403
            ? 9
            : std::clamp(extensionWidth_ + (value == 0x2402 ? 1 : -1), 1, 20);
    return true;
  }
  if (control && value == KEY_F(5)) {
    createArchiveCommand(true);
    return true;
  }
  if (!tree && value == KEY_F(5) && !alt && !control) {
    createArchiveCommand(false);
    return true;
  }
  if (control && value == KEY_F(6)) {
    if (other_) {
      s.tags.unite(other_->session.tags);
      other_->session.tags = s.tags;
      s.rebuild();
      other_->session.rebuild();
    }
    return true;
  }
  if (tree && control &&
      (value == KEY_F(7) || value == KEY_F(8) || value == KEY_F(9))) {
    s.tagBranch(value == KEY_F(7) ? 1 : value == KEY_F(8) ? 0 : 2);
    return true;
  }
  if (control && (value == KEY_F(4) || value == KEY_ENTER || value == 13)) {
    if (tree) {
      if (!s.currentDirectory().loaded)
        load(false);
      else
        s.enter(View::Directory, true);
    } else
      s.toggleTagsOnly();
    return true;
  }
  if (alt && value == KEY_F(4)) {
    filterCommand();
    return true;
  }
  if (alt && value == KEY_F(3)) {
    load(false, false);
    return true;
  }
  if (alt && letter == 'i') {
    info_ = !info_;
    return true;
  }
  if (alt && letter == 's') {
    sortCommand();
    return true;
  }
  if (alt && letter == 'g' && tree) {
    graftCommand();
    return true;
  }
  if (alt && letter == 'p' && tree) {
    pruneCommand();
    return true;
  }
  if (alt && letter == 'k' && !tree) {
    filterCommand();
    return true;
  }
  if ((letter == 'c' || letter == 'm') && !tree) {
    transfer(letter == 'm', control, alt);
    return true;
  }
  if (letter == 'c' && tree) {
    compareDirectoryCommand(alt);
    return true;
  }
  if ((letter == 'd' || value == KEY_DC) && !alt) {
    removeItems(control);
    return true;
  }
  if (letter == 'm' && tree && !alt) {
    mkdirCommand();
    return true;
  }
  if (letter == 'r' && !alt) {
    renameCommand(control);
    return true;
  }
  if (letter == 'h' && tree && !alt) {
    linkCommand();
    return true;
  }
  if (letter == 'g' && tree && !alt) {
    globalCommand(control);
    return true;
  }
  if (letter == 'x') {
    auto command = ask("EXECUTE command (empty opens shell): ");
    if (command)
      shellCommand(*command);
    return true;
  }
  if (letter == 's' && control && !tree) {
    batchSearch();
    return true;
  }
  if (letter == 'j' && !tree) {
    compareCommand(control);
    return true;
  }
  if (letter == 'i' && control) {
    int pick =
        choose("INVERT", {"Invert tags in current scope", "Invert Filespec"});
    if (pick < 0)
      return true;
    if (pick == 1) {
      s.filespec.inverted = !s.filespec.inverted;
      s.rebuild();
    } else {
      for (const auto &file : s.files()) {
        if (s.tags.contains(file.path))
          s.tags.remove(file.path);
        else
          s.tags.insert(file.path);
      }
      s.rebuild();
    }
    return true;
  }
  if (control && (letter == 't' || letter == 'u')) {
    s.setTag(letter == 't', true);
    return true;
  }
  if (control && (letter == 'b' || (tree && letter == 's'))) {
    s.enter(letter == 'b' ? View::Branch : View::Showall, true);
    return true;
  }
  if (letter == 'a' && !alt && !control && tree) {
    availability();
    return true;
  }
  if ((letter == 'a' || letter == 'n') && control) {
    beginMetadata(letter == 'n', true);
    return true;
  }
  if (value == '|') {
    spellCommand();
    return true;
  }
  if (value == '?' || value == '/') {
    statsCommand();
    return true;
  }
  if (value == '<' || value == '>' || value == ',' || value == '.') {
    cycleRoot(value == '<' || value == ',' ? -1 : 1);
    return true;
  }
  if (tree && (value == KEY_BTAB || (value == 9 && (control || !other_)))) {
    s.sibling(value == KEY_BTAB ? -1 : 1);
    return true;
  }
  if (value == 27 && !tree && globalReturn_) {
    roots_.insert(globalReturn_->session.root, *globalReturn_);
    publishGlobal();
    globalReturn_->session.filespec = s.filespec;
    globalReturn_->session.sort = s.sort;
    active_ = *globalReturn_;
    globalReturn_.reset();
    return true;
  }
  if (!tree && (value == 10 || value == 13 || value == KEY_ENTER) &&
      s.currentFile() && archiveName(s.currentFile()->path)) {
    archiveOpen(s.currentFile()->path);
    return true;
  }
  if (letter == 'v' && !tree) {
    if (control) {
      viewPaths_.clear();
      for (const auto &file : s.files())
        if (s.tags.contains(file.path))
          viewPaths_ << file.path;
      if (viewPaths_.isEmpty()) {
        status_ = "No tagged files";
        return true;
      }
      viewSequenceIndex_ = 0;
      readDocument(viewPaths_.first());
    } else if (s.currentFile()) {
      if (alt) {
        auto command = environment("LTREE_ALT_VIEWER");
        if (command.isEmpty())
          status_ = "Set LTREE_ALT_VIEWER to a terminal program";
        else
          shellCommand(command + " " + shellQuote(s.currentFile()->path));
      } else if (archiveName(s.currentFile()->path))
        archiveOpen(s.currentFile()->path, true);
      else
        readDocument(s.currentFile()->path);
    }
    return true;
  }
  if (letter == 'f' && !alt && !control) {
    auto value = ask("FILESPEC: ", s.filespec.text(), "filespec");
    if (value) {
      String error;
      if (s.filespec.set(*value, &error)) {
        s.rebuild();
        filespecHistory_ = histories_["filespec"].entries;
      } else
        status_ = error;
    }
    return true;
  }
  if (literal && !alt && !control &&
      ((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))) {
    auto spell = tree ? s.treeSpell : s.fileSpell;
    const char32_t cp = value;
    spell += String::fromUcs4(&cp, 1);
    if (!s.spell(spell))
      status_ = "No match";
    return true;
  }
  if (value == '"') {
    auto spell = tree ? s.treeSpell : s.fileSpell;
    if (!s.spell(spell, true))
      status_ = "No more matches";
    return true;
  }
  return false;
}

bool statisticsVisible(int w, bool focused) const {
  return w >= 95 &&
         (!other_ || statisticsMode_ == 2 || (statisticsMode_ == 1 && focused));
}
void drawSidebar(const Pane &pane, int x, int w) {
  const auto &s = pane.session;
  const auto counts = loggedStatistics(s);
  auto stats = systemStatistics(s.directory);
  StringList lines{"FILE " + s.filespec.text(),
                    "",
                    "DISK " + stats.filesystem,
                    "Available",
                    String::number(stats.available),
                    "",
                    "LOGGED Statistics",
                    "Total",
                    String("Files %1").arg(counts.totalFiles),
                    String("Bytes %1").arg(counts.totalBytes),
                    "Matching",
                    String("Files %1").arg(counts.matchingFiles),
                    String("Bytes %1").arg(counts.matchingBytes),
                    "Tagged",
                    String("Files %1").arg(counts.taggedFiles),
                    String("Bytes %1").arg(counts.taggedBytes),
                    "",
                    "Current Directory",
                    FileInfo(s.directory).fileName()};
  if (s.view != View::Tree && s.currentFile()) {
    lines << "Current File" << s.currentFile()->name
          << String("Bytes %1").arg(s.currentFile()->size);
  }
  for (int y = 1; y <= end(); ++y)
    text(x, y, "│", 1, 1);
  text(x, 1, "┬", 1, 1);
  if (s.view == View::Tree)
    text(x, divider(), "┼", 1, 1);
  text(x, end(), "┴", 1, 1);
  for (int i = 0; i < lines.size() && i + 2 < end(); ++i)
    text(x + 1, i + 2, lines[i],
         i == 0 || lines[i].contains("Statistics") ? 1 : 2, w - 2);
}

void publishGlobal() {
  if (!globalReturn_)
    return;
  globalReturn_->session.syncGlobalFrom(active_.session);
  for (auto it = roots_.begin(); it != roots_.end(); ++it)
    it->session.syncGlobalFrom(active_.session);
  if (other_)
    other_->session.syncGlobalFrom(active_.session);
}
bool documentReload_ = false, previewForward_ = false, previewDrawing_ = false,
     documentPreview_ = false;
int documentTop_ = 0;
Index previewHit_ = -1, previewHitLength_ = 0;
void forwardPreviewKey(int key, bool alt = false, bool literal = false) {
  if (previewReading_ || previewed_.bytes.isEmpty())
    return;
  auto saved = std::move(viewed_);
  auto path = viewerPath_;
  auto snapshot = viewerSnapshot_;
  int top = viewerTop_;
  bool hex = hex_;
  Index hit = hit_, length = hitLength_;
  viewed_ = previewed_;
  viewerPath_ = previewPath_;
  viewerSnapshot_ = hexSnapshot(previewPath_);
  viewerTop_ = previewTop_;
  hex_ = previewHex_;
  hit_ = previewHit_;
  hitLength_ = previewHitLength_;
  viewerSearch_ = rememberedSearch_;
  rebuildViewerLines(std::max(1, width() - listWidth() - 1));
  previewForward_ = true;
  extendedViewerKey(key, alt, literal);
  previewForward_ = false;
  previewed_ = viewed_;
  previewTop_ = viewerTop_;
  previewHex_ = hex_;
  previewAuto_ = false;
  previewHit_ = hit_;
  previewHitLength_ = hitLength_;
  if (editing_) {
    viewer_ = true;
    return;
  }
  viewed_ = std::move(saved);
  viewerPath_ = path;
  viewerSnapshot_ = snapshot;
  viewerTop_ = top;
  hex_ = hex;
  hit_ = hit;
  hitLength_ = length;
  rebuildViewerLines();
}
void drawStyledPreview(int x, int w) {
  previewDrawing_ = true;
  auto doc = viewerCharset_ ? decodeBytes(previewed_.bytes, viewerCharset_)
                            : previewed_;
  if (previewHex_)
    drawDocument(x, 3, end() - 4, w, doc, true, previewTop_);
  else if (viewerMode_ == 1) {
    int bytes = bytesPerRow(w);
    for (int r = 0; r < end() - 4; ++r) {
      int offset = (previewTop_ + r) * bytes;
      if (offset >= doc.bytes.size())
        break;
      String line;
      for (unsigned char ch : doc.bytes.mid(offset, bytes))
        line += String::number(ch).rightJustified(3) + " ";
      text(x, 3 + r, line, viewerTextPair_, w);
    }
  } else {
    auto saved = std::move(viewed_);
    viewed_ = doc;
    rebuildViewerLines(w);
    previewTop_ =
        std::clamp(previewTop_, 0, std::max(0, int(viewerLines_.size()) - 1));
    int y = 3;
    if (viewerRuler_) {
      String ruler;
      for (int i = 0; i < w; ++i)
        ruler += Char('0' + ((viewerHorizontal_ + i + 1) % 10));
      text(x, y++, ruler, 1, w);
    }
    for (int r = 0; y + r < end() - 1 && previewTop_ + r < viewerLines_.size();
         ++r) {
      const auto &line = viewerLines_[previewTop_ + r];
      text(x, y + r, line.mid(viewerHorizontal_),
           viewerHighlight_ == r ? 3 : viewerTextPair_, w);
    }
    viewed_ = std::move(saved);
    rebuildViewerLines();
  }
  previewDrawing_ = false;
}

String operationRoot(const String &path) const {
  if (isWithin(path, active_.session.root))
    return active_.session.root;
  if (other_ && isWithin(path, other_->session.root))
    return other_->session.root;
  for (auto it = roots_.cbegin(); it != roots_.cend(); ++it)
    if (isWithin(path, it.key()))
      return it.key();
  return FileInfo(path).absolutePath();
}
void spellCommand() {
  String input;
  while (!stopped) {
    draw();
    fill(0, rows_ - 2, columns_, 1);
    text(0, rows_ - 2, "SPELL: " + input, 2);
    commands(
        0, rows_ - 1,
        "[Type] Match [Backspace] Erase [\"/PgDn] Next [Enter/Esc] Return");
    wrefresh(stdscr);
    bool literal;
    int k = nextKey(&literal);
    if (k == 27 || k == 10 || k == 13 || k == KEY_ENTER)
      return;
    if (k == KEY_BACKSPACE || k == 127 || k == 8)
      input.chop(1);
    else if (k == '\"' || k == KEY_NPAGE) {
      active_.session.spell(input, true);
      continue;
    } else if (literal && k >= 32 && k != 127) {
      const char32_t cp = k;
      input += String::fromUcs4(&cp, 1);
    }
    if (!active_.session.spell(input))
      status_ = "No match";
  }
}

void saveViewerHistory() {
  JsonArray search, gather;
  for (auto it = histories_["search"].entries.crbegin();
       it != histories_["search"].entries.crend(); ++it)
    search.append(*it);
  for (auto it = histories_["gather"].entries.crbegin();
       it != histories_["gather"].entries.crend(); ++it)
    gather.append(*it);
  SaveFile file(historyDirectory() + "/viewer-history.json");
  if (!file.open(IO::WriteOnly))
    return;
  file.setPermissions(File::ReadOwner | File::WriteOwner);
  auto bytes =
      JsonDocument(
          JsonObject{{"version", 1}, {"search", search}, {"gather", gather}})
          .toJson();
  if (file.write(bytes) == bytes.size())
    file.commit();
}

String terminalByteGlyph(uint8 value) const {
  if (viewerCharset_ != 2 && viewerCharset_ != 6)
    return dosGlyph(value);
  static Map<int, String> tables;
  if (!tables.contains(viewerCharset_)) {
    Bytes input;
    for (int i = 0; i < 256; ++i)
      input.append(char(i));
    Bytes output(1040, '\0');
    char *in = input.data(), *out = output.data();
    size_t left = input.size(), capacity = output.size();
    auto converter =
        iconv_open("UTF-8", viewerCharset_ == 2 ? "WINDOWS-1252" : "IBM037");
    if (converter != iconv_t(-1)) {
      while (left) {
        if (iconv(converter, &in, &left, &out, &capacity) != size_t(-1))
          break;
        *out++ = '?';
        --capacity;
        ++in;
        --left;
      }
      iconv_close(converter);
    }
    tables[viewerCharset_] =
        String::fromUtf8(output.constData(), out - output.data());
  }
  return String(value < tables[viewerCharset_].size()
                     ? tables[viewerCharset_][value]
                     : Char('.'));
}

void showHelp(HelpTopic topic) {
  auto previousPrompt = std::exchange(prompt_, Prompt::None);
  helpDocument_.open(topic);
  help_ = true;
  while (help_ && !stopped) {
    draw();
    int key = nextKey();
    if (key >= 0)
      helpKey(key);
  }
  prompt_ = previousPrompt;
}

std::optional<String> browseDirectory(String path) {
  path = absolute(path);
  if (!FileInfo(path).isDir())
    path = active_.session.directory;
  while (!stopped) {
    StringList labels{"Use this directory", ".."};
    auto children = DirectoryPath(path).entryList(DirectoryPath::Dirs | DirectoryPath::NoDotAndDotDot |
                                             DirectoryPath::NoSymLinks | DirectoryPath::Readable,
                                         DirectoryPath::Name);
    labels += children;
    int picked = choose("BROWSE: " + path, labels);
    if (picked < 0)
      return {};
    if (picked == 0)
      return path;
    if (picked == 1)
      path = FileInfo(path).absolutePath();
    else
      path = DirectoryPath(path).filePath(children[picked - 2]);
  }
  return {};
}
