int viewerHorizontal_ = 0, viewerMode_ = 0, viewerCharset_ = 0, viewerTab_ = 6,
    viewerMenu_ = 0, viewerHighlight_ = -1;
bool viewerWrap_ = false, viewerRuler_ = false, viewerMask_ = false,
     viewerHitSkip_ = true, continuous_ = false, scrollLoop_ = false;
int scrollSpeed_ = 0;
qint64 scrollNext_ = 0, reloadNext_ = 0;
QMap<int, qsizetype> bookmarks_;
qsizetype hit_ = -1, hitLength_ = 0, gatherStart_ = -1;
QString gather_, clipboard_;
int viewerGap_ = 0, viewerTextPair_ = 2, viewerBackground_ = 0;
SearchOptions viewerSearch_;
QString viewerSearchError_;
QVector<QString> viewerLines_;
QVector<qsizetype> viewerOffsets_;
ViewDocument decodeBytes(const QByteArray &bytes, int charset = 0) const {
  ViewDocument doc;
  doc.bytes = bytes;
  if (charset == 1 || charset == 2 || charset == 6) {
    const char *encoding = charset == 1   ? "CP437"
                           : charset == 2 ? "WINDOWS-1252"
                                          : "IBM037";
    iconv_t converter = iconv_open("UTF-8", encoding);
    if (converter != iconv_t(-1)) {
      QByteArray output(bytes.size() * 4 + 16, '\0');
      char *input = const_cast<char *>(bytes.constData()), *out = output.data();
      size_t left = bytes.size(), capacity = output.size();
      if (iconv(converter, &input, &left, &out, &capacity) != size_t(-1))
        doc.text = QString::fromUtf8(output.constData(), out - output.data());
      iconv_close(converter);
    }
    doc.encoding = QString::fromLatin1(encoding);
  } else {
    auto encoding = charset == 3   ? QStringConverter::Utf8
                    : charset == 4 ? QStringConverter::Utf16LE
                    : charset == 5
                        ? QStringConverter::Utf16BE
                        : QStringConverter::encodingForData(bytes).value_or(
                              QStringConverter::Utf8);
    QStringDecoder decoder(encoding);
    doc.text = decoder.decode(bytes);
    char16_t tail[8];
    const auto final = decoder.finalize(tail, 8);
    if (decoder.hasError() ||
        final.error != QStringConverter::FinalizeResultError::NoError) {
      doc.text = QString::fromLatin1(bytes);
      doc.encoding = "Latin-1 (fallback)";
    } else
      doc.encoding =
          QString::fromLatin1(QStringConverter::nameForEncoding(encoding));
  }
  doc.text.replace("\r\n", "\n").replace('\r', '\n');
  doc.lines << 0;
  for (qsizetype i = 0; i < doc.text.size(); ++i)
    if (doc.text[i] == '\n' && i + 1 < doc.text.size())
      doc.lines << i + 1;
  return doc;
}
void setMemoryDocument(const QByteArray &bytes, const QString &label) {
  if (bytes.size() > 32 * 1024 * 1024) {
    status_ = "Viewer limit: 32 MiB";
    return;
  }
  viewed_ = decodeBytes(bytes);
  viewerPath_ = label;
  viewer_ = true;
  viewerTop_ = 0;
  viewerSnapshot_ = {};
  hex_ = terminalBinaryData(bytes);
  resetViewer();
}
std::pair<qsizetype, qsizetype> findInDocument(const ViewDocument &doc,
                                               const SearchOptions &options,
                                               qsizetype from, bool backwards,
                                               bool byteView = false) {
  viewerSearchError_.clear();
  if (byteView && options.mode != SearchMode::Hex) {
    ViewDocument raw = doc;
    bool latin = options.mode == SearchMode::Text;
    if (latin) {
      raw.text = QString::fromLatin1(doc.bytes);
      raw.encoding = "Latin-1 (fallback)";
    } else {
      raw = decodeBytes(doc.bytes);
      const auto encoding =
          QStringConverter::encodingForName(raw.encoding.toLatin1());
      if (encoding)
        raw.text = QStringDecoder(*encoding).decode(doc.bytes);
      else
        raw.text = QString::fromLatin1(doc.bytes);
    }
    raw.lines = {0};
    for (qsizetype i = 0; i < raw.text.size(); ++i)
      if (raw.text[i] == '\n')
        raw.lines << i + 1;
    QVector<qsizetype> offsets;
    qsizetype byte = 0;
    if (!latin && doc.bytes.startsWith(QByteArray::fromHex("efbbbf")))
      byte = 3;
    else if (!latin && (doc.bytes.startsWith(QByteArray::fromHex("fffe")) ||
                        doc.bytes.startsWith(QByteArray::fromHex("feff"))))
      byte = 2;
    bool utf16 =
        !latin && raw.encoding.startsWith("UTF-16", Qt::CaseInsensitive);
    for (qsizetype i = 0; i < raw.text.size(); ++i) {
      offsets << byte;
      if (latin || raw.encoding == "Latin-1 (fallback)")
        ++byte;
      else if (utf16)
        byte += 2;
      else if (raw.text[i].isHighSurrogate() && i + 1 < raw.text.size() &&
               raw.text[i + 1].isLowSurrogate()) {
        offsets << byte;
        ++i;
        byte += 4;
      } else
        byte += QString(raw.text[i]).toUtf8().size();
    }
    offsets << doc.bytes.size();
    qsizetype index = std::lower_bound(offsets.cbegin(), offsets.cend(), from) -
                      offsets.cbegin();
    if (backwards && index > 0 && offsets.value(index) > from)
      --index;
    auto found = findInDocument(raw, options, index, backwards);
    if (found.first < 0)
      return found;
    return {offsets.value(found.first),
            offsets.value(found.first + found.second, doc.bytes.size()) -
                offsets.value(found.first)};
  }
  if (options.query.isEmpty())
    return {-1, 0};
  if (options.mode == SearchMode::Hex) {
    QByteArray hex = options.query.toLatin1();
    hex.replace(" ", "").replace("\t", "");
    auto needle = QByteArray::fromHex(hex);
    int index = backwards ? doc.bytes.lastIndexOf(needle, from)
                          : doc.bytes.indexOf(needle, from);
    return {index, needle.size()};
  }
  if (options.mode == SearchMode::Regex) {
    auto expression = searchExpression(options);
    if (backwards) {
      for (int l = int(doc.lines.size()) - 1; l >= 0; --l) {
        qsizetype start = doc.lines[l], end = l + 1 < doc.lines.size()
                                                  ? doc.lines[l + 1] - 1
                                                  : doc.text.size();
        if (start > from)
          continue;
        auto match = matchRegexLine(
            expression, QStringView(doc.text).mid(start, end - start),
            std::min(from - start, end - start), true);
        if (!match.error.isEmpty()) {
          viewerSearchError_ = match.error;
          return {-1, 0};
        }
        if (match.start >= 0)
          return {start + match.start, match.length};
      }
    } else {
      for (int l = 0; l < doc.lines.size(); ++l) {
        qsizetype start = doc.lines[l], end = l + 1 < doc.lines.size()
                                                  ? doc.lines[l + 1] - 1
                                                  : doc.text.size();
        if (end < from)
          continue;
        auto match = matchRegexLine(
            expression, QStringView(doc.text).mid(start, end - start),
            std::max(qsizetype(0), from - start), false);
        if (!match.error.isEmpty()) {
          viewerSearchError_ = match.error;
          return {-1, 0};
        }
        if (match.start >= 0)
          return {start + match.start, match.length};
      }
    }
    return {-1, 0};
  }
  QString subject = doc.text, needle = options.query;
  auto sensitivity =
      options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
  if (options.mode == SearchMode::Text) {
    if (doc.encoding == "Latin-1 (fallback)")
      needle = QString::fromLatin1(options.query.toUtf8());
    if (!options.caseSensitive) {
      auto fold = [](QString &text) {
        for (QChar &ch : text)
          if (ch >= 'A' && ch <= 'Z')
            ch = QChar(ch.unicode() + 32);
      };
      fold(subject);
      fold(needle);
    }
    sensitivity = Qt::CaseSensitive;
  }
  if (!needle.startsWith('*') &&
      needle.mid(1, needle.size() - 2).contains('*')) {
    QString pattern;
    for (int i = 0; i < needle.size(); ++i)
      pattern += needle[i] == '*' && i > 0 && i + 1 < needle.size()
                     ? "[^\\r\\n]*?"
                     : QRegularExpression::escape(QString(needle[i]));
    QRegularExpression expression(
        "(*LIMIT_MATCH=1000000)(*LIMIT_DEPTH=1000)(*LIMIT_HEAP=8192)" + pattern,
        sensitivity == Qt::CaseSensitive
            ? QRegularExpression::NoPatternOption
            : QRegularExpression::CaseInsensitiveOption);
    std::pair<qsizetype, qsizetype> result{-1, 0};
    for (qsizetype offset = backwards ? 0 : from;
         offset >= 0 && offset <= subject.size();) {
      auto match = expression.match(subject, offset);
      if (!match.isValid()) {
        viewerSearchError_ = "Wildcard match limit exceeded";
        return {-1, 0};
      }
      if (!match.hasMatch() || (backwards && match.capturedStart() > from))
        break;
      result = {match.capturedStart(), match.capturedLength()};
      if (!backwards)
        break;
      offset = match.capturedStart() + 1;
    }
    return result;
  }
  qsizetype index = backwards ? subject.lastIndexOf(needle, from, sensitivity)
                              : subject.indexOf(needle, from, sensitivity);
  return {index, needle.size()};
}
void resetViewer() {
  viewerHorizontal_ = viewerMenu_ = 0;
  viewerHighlight_ = -1;
  hit_ = -1;
  gatherStart_ = -1;
  viewerSearch_ = rememberedSearch_;
  bookmarks_.clear();
  continuous_ = false;
  scrollSpeed_ = 0;
  rebuildViewerLines();
  if (!viewerSearch_.query.isEmpty())
    viewerFind(false, true);
}
void rebuildViewerLines(int displayWidth = -1) {
  viewerLines_.clear();
  viewerOffsets_.clear();
  if (viewerMode_ == 2) {
    viewed_ = decodeBytes(viewed_.bytes, viewerCharset_);
    QString clean;
    for (unsigned char byte : viewed_.bytes)
      if (byte >= 32 && byte <= 126)
        clean += QChar(byte);
    viewed_.text = clean;
    viewed_.lines = {0};
  }
  const int w = std::max(1, (displayWidth < 0 ? columns_ : displayWidth) - 7);
  for (qsizetype l = 0; l < viewed_.lines.size(); ++l) {
    qsizetype start = viewed_.lines[l], end = l + 1 < viewed_.lines.size()
                                                  ? viewed_.lines[l + 1]
                                                  : viewed_.text.size();
    QString line = viewed_.text.mid(start, end - start);
    line.remove('\n');
    QString expanded;
    for (const auto ch : line) {
      if (ch == '\t')
        expanded += QString(viewerTab_ - (expanded.size() % viewerTab_), ' ');
      else
        expanded += ch;
    }
    if (viewerMode_ == 2) {
      QString clean;
      for (const auto ch : expanded)
        if (ch.isPrint())
          clean += ch;
      expanded = clean;
    }
    if (viewerMask_ && viewerMode_ != 2) {
      for (QChar &ch : expanded) {
        int code = ch.unicode();
        if (code < 32 || (code >= 128 && code < 160))
          ch = ' ';
        else if (code >= 160 && code <= 255)
          ch = QChar(code & 127);
      }
    }
    if (viewerWrap_ && expanded.size() > w) {
      for (int at = 0; at < expanded.size(); at += w) {
        viewerLines_ << expanded.mid(at, w);
        viewerOffsets_ << start + at;
      }
    } else {
      viewerLines_ << expanded;
      viewerOffsets_ << start;
    }
  }
}
qsizetype viewerPosition() const {
  if (hex_ || viewerMode_ == 1)
    return qsizetype(viewerTop_) * bytesPerRow(columns_);
  return viewerOffsets_.isEmpty()
             ? 0
             : viewerOffsets_[std::clamp(viewerTop_ +
                                             std::max(0, viewerHighlight_),
                                         0, int(viewerOffsets_.size()) - 1)];
}
void gotoViewer(qsizetype position) {
  if (hex_ || viewerMode_ == 1)
    viewerTop_ = int(position / bytesPerRow(columns_));
  else {
    auto it = std::upper_bound(viewerOffsets_.cbegin(), viewerOffsets_.cend(),
                               position);
    viewerTop_ = std::max(0, int(it - viewerOffsets_.cbegin()) - 1);
    if (!viewerWrap_)
      viewerHorizontal_ =
          std::max(0, int(position - viewerOffsets_.value(viewerTop_)) -
                          std::max(1, columns_ / 2));
  }
}
void viewerFind(bool backwards, bool initial = false, bool pageOnly = false) {
  if (viewerSearch_.query.isEmpty()) {
    if (!searchPrompt(viewerSearch_))
      return;
    rememberedSearch_ = viewerSearch_;
  }
  qsizetype from =
      initial     ? backwards ? ((hex_ || viewerMode_ == 1 ||
                                  viewerSearch_.mode == SearchMode::Hex)
                                     ? viewed_.bytes.size()
                                     : viewed_.text.size()) -
                                    1
                              : 0
      : hit_ >= 0 ? hit_ + (backwards ? -1 : std::max(qsizetype(1), hitLength_))
                  : viewerPosition();
  if (from < 0) {
    status_ = "No previous match";
    return;
  }
  if (pageOnly)
    from = viewerPosition();
  auto found = findInDocument(viewed_, viewerSearch_, from, backwards,
                              hex_ || viewerMode_ == 1);
  if (found.first < 0) {
    if (!viewerSearchError_.isEmpty()) {
      status_ = viewerSearchError_;
      return;
    }
    if (!viewPaths_.isEmpty() &&
        viewSequenceIndex_ + (backwards ? -1 : 1) >= 0 &&
        viewSequenceIndex_ + (backwards ? -1 : 1) < viewPaths_.size()) {
      sequenceFile(backwards ? -1 : 1);
      return;
    }
    status_ = "No more matches";
    return;
  }
  if (pageOnly) {
    qsizetype end =
        hex_ ? viewerPosition() + (rows_ - 5) * bytesPerRow(columns_)
             : viewerOffsets_.value(std::min(viewerTop_ + rows_ - 5,
                                             int(viewerOffsets_.size()) - 1),
                                    viewed_.text.size());
    if (found.first > end) {
      status_ = "No match on this page";
      return;
    }
  }
  hit_ = found.first;
  hitLength_ = found.second;
  if (viewerSearch_.mode == SearchMode::Hex)
    hex_ = true;
  gotoViewer(hit_);
  status_ = QString("Match at %1").arg(hit_);
}
void drawExtendedViewer() {
  text(0, 0, pathLabel(viewerPath_, columns_), 1, columns_);
  const int h = rows_ - 5;
  if (hex_) {
    drawDocument(0, 1, h, columns_, viewed_, true, viewerTop_);
    if (hit_ >= 0) {
      int bytes = bytesPerRow(columns_);
      for (qsizetype at = hit_;
           at < hit_ + hitLength_ && at < viewed_.bytes.size(); ++at) {
        int row = at / bytes - viewerTop_;
        if (row >= 0 && row < h)
          text(10 + (at % bytes) * 3, row + 1,
               QString("%1")
                   .arg(quint8(viewed_.bytes[at]), 2, 16, QChar('0'))
                   .toUpper(),
               3, 2);
      }
    }
  } else if (viewerMode_ == 1) {
    int bytes = bytesPerRow(columns_);
    for (int r = 0; r < h; ++r) {
      qsizetype from = qsizetype(viewerTop_ + r) * bytes;
      if (from >= viewed_.bytes.size())
        break;
      QString line;
      for (unsigned char ch : viewed_.bytes.mid(from, bytes))
        line += QString::number(ch).rightJustified(3) + " ";
      text(0, r + 1, QString::number(from).rightJustified(8) + " " + line, 2,
           columns_);
    }
  } else {
    if (viewerWrap_)
      rebuildViewerLines();
    viewerTop_ =
        std::clamp(viewerTop_, 0, std::max(0, int(viewerLines_.size()) - 1));
    int startY = viewerRuler_ ? 2 : 1;
    if (viewerRuler_) {
      QString ruler;
      for (int i = 0; i < columns_ - 7; ++i)
        ruler += QChar('0' + ((viewerHorizontal_ + i + 1) % 10));
      text(7, 1, ruler, 1, columns_ - 7);
    }
    for (int r = 0;
         r < h - (viewerRuler_ ? 1 : 0) && viewerTop_ + r < viewerLines_.size();
         ++r) {
      int index = viewerTop_ + r;
      const auto &line = viewerLines_[index];
      const int pair = viewerHighlight_ == r ? 3 : viewerTextPair_;
      if (pair == 3)
        fill(0, startY + r, columns_, 3);
      text(0, startY + r, QString::number(index + 1).rightJustified(5), 1, 5);
      text(7, startY + r, line.mid(viewerHorizontal_), pair, columns_ - 7);
      if (hit_ >= viewerOffsets_[index] &&
          hit_ < viewerOffsets_[index] + line.size() &&
          viewerSearch_.mode != SearchMode::Hex) {
        int x = 7 + int(hit_ - viewerOffsets_[index]) - viewerHorizontal_;
        if (x >= 7 && x < columns_)
          text(x, startY + r,
               line.mid(int(hit_ - viewerOffsets_[index]), hitLength_), 3,
               columns_ - x);
      }
    }
  }
  int count =
      hex_ || viewerMode_ == 1
          ? std::max(1, int((viewed_.bytes.size() + bytesPerRow(columns_) - 1) /
                            bytesPerRow(columns_)))
          : std::max(1, int(viewerLines_.size()));
  for (int y = 1; y <= h; ++y)
    text(columns_ - 1, y, "│", 1, 1);
  text(columns_ - 1,
       1 + std::clamp(viewerTop_ * std::max(0, h - 1) / count, 0, h - 1), "█",
       1, 1);
  if (keyboard_.menu(viewerMenu_) == 1)
    commands(0, rows_ - 3,
             "CTRL VIEW: [0..9] Bookmark [S] Previous query [V] Sequence [F3] "
             "Reload continuously [C] Copy [A] Append [D] Clear");
  else if (keyboard_.menu(viewerMenu_) == 2)
    commands(0, rows_ - 3,
             "ALT VIEW: [0..9] Bookmark [V] Viewer [E] Editor [X] Shell [P] "
             "Print [F5/F6] Colors");
  else
    commands(0, rows_ - 3,
             "VIEW: [A]lpha [C]harset [D]ump [E]dit [G]ather [H]ex [J]unk "
             "[L]ine [M]ask [O]ffset [R]uler [Tab] Tab [W]rap");
  commands(0, rows_ - 2,
           "[F/B/S/F9] Find [Space/+/-] Next/Back [F3] Reload [F8] Skip [F10] "
           "Menus [N/P/U] File [X] Shell [Esc] Return");
  text(0, rows_ - 1,
       QString("%1 %2  %3/%4  %5 bytes  Tab %6  %7  %8")
           .arg(hex_               ? "HEX"
                : viewerMode_ == 1 ? "DUMP"
                : viewerMode_ == 2 ? "JUNK"
                                   : "ALPHA",
                viewed_.encoding)
           .arg(viewerPosition())
           .arg(hex_ ? viewed_.bytes.size() : viewed_.text.size())
           .arg(viewed_.bytes.size())
           .arg(viewerTab_)
           .arg(viewerHitSkip_ ? "hit" : "page",
                viewerMask_ ? "mask" : "no mask"),
       1);
}
void viewerOffset() {
  bool line = !(hex_ || viewerMode_ == 1), relative = false;
  int base = hex_ ? 16 : 10;
  auto value = ask(
      "OFFSET / line: ", {}, {},
      [&] {
        return QString("[F2] %1 [F4] Base %2 [F5] %3 [Enter] Jump [Esc] Cancel")
            .arg(line ? "Line" : "Offset")
            .arg(base)
            .arg(relative ? "Relative" : "Absolute");
      },
      [&](int k, QString &) {
        if (k == KEY_F(2))
          line = !line;
        else if (k == KEY_F(4))
          base = base == 16 ? 10 : 16;
        else if (k == KEY_F(5))
          relative = !relative;
        else
          return false;
        return true;
      });
  if (!value)
    return;
  bool ok;
  qsizetype at = value->toLongLong(&ok, base);
  if (!ok) {
    status_ = "Invalid offset";
    return;
  }
  if (line)
    viewerTop_ = std::clamp(int(at) + (relative ? viewerTop_ : -1), 0,
                            std::max(0, int(viewerLines_.size()) - 1));
  else
    gotoViewer(std::clamp(
        at + (relative ? viewerPosition() : 0), qsizetype(0),
        qsizetype(hex_ ? viewed_.bytes.size() : viewed_.text.size())));
}
void viewerGather() {
  if (gatherStart_ < 0) {
    gatherStart_ = viewerPosition();
    status_ = "Gather start marked; G again marks the end";
    return;
  }
  qsizetype start = std::min(gatherStart_, viewerPosition()),
            end = std::max(gatherStart_, viewerPosition());
  if (!hex_) {
    int line =
        std::upper_bound(viewed_.lines.cbegin(), viewed_.lines.cend(), end) -
        viewed_.lines.cbegin();
    end = (line < viewed_.lines.size() ? viewed_.lines[line]
                                       : viewed_.text.size()) -
          1;
  }
  gatherStart_ = -1;
  QByteArray selection =
      hex_ ? viewed_.bytes.mid(start, end - start + 1)
           : viewed_.text.mid(start, end - start + 1).toUtf8();
  int pick = choose("GATHER selection",
                    {"Write to file", "Append to file", "Copy to clipboard",
                     "Append to clipboard", "Clear clipboard"});
  if (pick < 0)
    return;
  if (pick >= 2) {
    if (pick == 4)
      clipboard_.clear();
    else {
      if (pick == 2)
        clipboard_.clear();
      clipboard_ += QString::fromUtf8(selection);
    }
    publishClipboard();
    return;
  }
  auto name = ask("GATHER to: ", gather_, "gather");
  if (!name)
    return;
  gather_ = *name;
  QFile file(absolute(*name));
  if (pick == 0 && QFileInfo::exists(file.fileName()) &&
      !confirm("Replace " + file.fileName() + "?"))
    return;
  if (!file.open(QIODevice::WriteOnly |
                 (pick == 1 ? QIODevice::Append : QIODevice::Truncate))) {
    status_ = file.errorString();
    return;
  }
  if (file.write(selection) != selection.size())
    status_ = file.errorString();
  else
    status_ = "Gather saved";
}
QString gatherSelection() const {
  if (hex_)
    return QString::fromLatin1(viewed_.bytes.toHex(' '));
  if (gatherStart_ >= 0) {
    qsizetype first = std::min(gatherStart_, viewerPosition()),
              last = std::max(gatherStart_, viewerPosition());
    return viewed_.text.mid(first, last - first + 1);
  }
  int at = std::clamp(viewerTop_ + std::max(0, viewerHighlight_), 0,
                      std::max(0, int(viewerLines_.size()) - 1));
  return viewerLines_.value(at) + "\n";
}
void publishClipboard() {
  QString program;
  QStringList args;
  if (!qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty()) {
    program = QStandardPaths::findExecutable("wl-copy");
  } else if (!qEnvironmentVariable("DISPLAY").isEmpty()) {
    program = QStandardPaths::findExecutable("xclip");
    args = {"-selection", "clipboard"};
  }
  if (program.isEmpty()) {
    status_ = "Selection stored internally; use Gather to write a file "
              "(desktop clipboard requires wl-copy or xclip)";
    return;
  }
  QProcess process;
  process.start(program, args);
  if (!process.waitForStarted()) {
    status_ = process.errorString();
    return;
  }
  process.write(clipboard_.toUtf8());
  process.closeWriteChannel();
  if (!process.waitForFinished(3000)) {
    process.kill();
    process.waitForFinished();
  }
  status_ = process.exitCode() == 0
                ? "Clipboard updated"
                : QString::fromUtf8(process.readAllStandardError());
}
void viewerPrint() {
  auto dest = ask("PRINT to file (or | command): ");
  if (!dest)
    return;
  QByteArray data;
  if (hex_ || viewerMode_ == 1) {
    for (qsizetype at = 0; at < viewed_.bytes.size(); at += 16) {
      QString line = QString("%1  ").arg(at, 8, 16, QChar('0')).toUpper();
      for (unsigned char byte : viewed_.bytes.mid(at, 16))
        line += hex_ ? QString("%1 ").arg(byte, 2, 16, QChar('0')).toUpper()
                     : QString::number(byte).rightJustified(3) + " ";
      data += (line + "\n").toUtf8();
    }
  } else
    data = viewed_.text.toUtf8();
  if (dest->startsWith('|')) {
    QProcess process;
    process.start("/bin/sh", {"-c", dest->mid(1)});
    if (!process.waitForStarted()) {
      status_ = process.errorString();
      return;
    }
    process.write(data);
    process.closeWriteChannel();
    while (!process.waitForFinished(100)) {
      QCoreApplication::processEvents();
      if (stopped) {
        process.terminate();
        break;
      }
    }
    status_ = process.exitCode() == 0
                  ? "Print completed"
                  : QString::fromUtf8(process.readAllStandardError());
  } else {
    QFile file(absolute(*dest));
    if (QFileInfo::exists(file.fileName()) &&
        !confirm("Replace " + file.fileName() + "?"))
      return;
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
      status_ = file.errorString();
    else
      status_ = "Print saved";
  }
}
void sequenceFile(int direction, bool untag = false) {
  if (inArchive_ && !viewerSnapshot_.valid) {
    if (untag && !archiveVisible_.isEmpty())
      archiveTags_.remove(
          archiveCatalog_.entries[archiveVisible_[archiveIndex_]].index);
    for (int i = archiveIndex_ + direction;
         i >= 0 && i < archiveVisible_.size(); i += direction) {
      auto entry = archiveCatalog_.entries[archiveVisible_[i]];
      if (entry.directory)
        continue;
      archiveIndex_ = i;
      auto bytes = task("Reading " + entry.path,
                        [this, entry](const Cancellation &cancel) {
                          return readArchiveMember(archivePath_, entry.index,
                                                   cancel, archivePassword_);
                        });
      if (bytes.error.isEmpty())
        setMemoryDocument(bytes.bytes, archivePath_ + " / " + entry.path);
      else
        status_ = bytes.error;
      return;
    }
    status_ = "No more archive members";
    return;
  }
  if (viewPaths_.isEmpty()) {
    for (const auto &file : active_.session.files())
      viewPaths_ << file.path;
    viewSequenceIndex_ = viewPaths_.indexOf(viewerPath_);
  }
  if (untag) {
    active_.session.tags.remove(viewerPath_);
    if (other_)
      other_->session.tags.remove(viewerPath_);
  }
  int next = viewSequenceIndex_ + direction;
  if (next < 0 || next >= viewPaths_.size()) {
    status_ = "No more files";
    return;
  }
  viewSequenceIndex_ = next;
  readDocument(viewPaths_[next]);
}
void viewerTick() {
  if (!viewer_ || editing_ || work_ != Work::None)
    return;
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (scrollSpeed_ && now >= scrollNext_) {
    ++viewerTop_;
    int count = hex_ ? (viewed_.bytes.size() + bytesPerRow(columns_) - 1) /
                           bytesPerRow(columns_)
                     : viewerLines_.size();
    if (viewerTop_ >= count)
      viewerTop_ = scrollLoop_ ? 0 : std::max(0, count - 1);
    scrollNext_ = now + 2000 / std::max(1, scrollSpeed_);
  }
  if (continuous_ && viewerSnapshot_.valid && now >= reloadNext_) {
    reloadNext_ = now + 1000;
    if (hexSnapshot(viewerPath_) != viewerSnapshot_)
      readDocument(viewerPath_);
  }
}
void extendedViewerKey(int value, bool alt, bool literal, bool directControl = false) {
  if (value >= 0x3000 && value < 0x4000) {
    value -= 0x3000;
    alt = true;
  }
  int letter = value >= 'A' && value <= 'Z' ? value + 32 : value;
  bool control = directControl || viewerMenu_ == 1;
  alt = alt || viewerMenu_ == 2;
  if (value == KEY_F(10)) {
    viewerMenu_ = (viewerMenu_ + 1) % 3;
    return;
  }
  if (value == 27 && viewerMenu_) {
    viewerMenu_ = 0;
    return;
  }
  viewerMenu_ = 0;
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
  if (value >= 0x2202 && value <= 0x2206) {
    scrollSpeed_ = 1 << (value - 0x2202);
    scrollNext_ = 0;
    return;
  }
  if (value == 0x2207) {
    scrollLoop_ = !scrollLoop_;
    return;
  }
  if (value > 0 && value < 27 && value != 9 && value != 10 && value != 13 &&
      value != 8) {
    control = true;
    letter = value + 'a' - 1;
  }
  if ((control || alt) && letter >= '0' && letter <= '9') {
    int mark = letter - '0';
    if (control) {
      bookmarks_[mark] = viewerPosition();
      status_ = "Bookmark set";
    } else if (bookmarks_.contains(mark))
      gotoViewer(bookmarks_[mark]);
    else
      status_ = "Bookmark is empty";
    return;
  }
  if (control && letter == 's') {
    auto &items = histories_["search"].entries;
    if (items.size() > 1) {
      viewerSearch_.query = items[viewerSearch_.query == items[0] ? 1 : 0];
      hit_ = -1;
      viewerFind(false, true);
    }
    return;
  }
  if (value == KEY_F(2) && !alt && !control) {
    clipboard_.clear();
    publishClipboard();
    return;
  }
  if (value == KEY_F(6) && !alt && !control) {
    viewerGap_ = (viewerGap_ + 1) % 4;
    status_ = QString("Clipboard gap: %1").arg(viewerGap_);
    return;
  }
  if ((value == KEY_F(4) || letter == 'g') && !alt && !control) {
    viewerGather();
    return;
  }
  if (value == KEY_F(5) && !alt && !control) {
    clipboard_ += QString(viewerGap_, '\n') + gatherSelection();
    publishClipboard();
    return;
  }
  if ((control && letter == 'c') || value == 0x2606) {
    clipboard_ = gatherSelection();
    publishClipboard();
    return;
  }
  if ((control && letter == 'a') || value == 0x2607) {
    clipboard_ += QString(viewerGap_, '\n') + gatherSelection();
    publishClipboard();
    return;
  }
  if (control && letter == 'd') {
    clipboard_.clear();
    publishClipboard();
    return;
  }
  if (control && letter == 'v') {
    viewPaths_.clear();
    for (const auto &file : active_.session.files())
      if (active_.session.tags.contains(file.path))
        viewPaths_ << file.path;
    viewSequenceIndex_ = viewPaths_.indexOf(viewerPath_);
    status_ = QString("View sequence: %1 tagged files").arg(viewPaths_.size());
    return;
  }
  if (control && value == KEY_F(3)) {
    continuous_ = !continuous_;
    reloadNext_ = 0;
    status_ = continuous_ ? "Continuous reload on" : "Continuous reload off";
    return;
  }
  if (alt && value == KEY_F(5)) {
    viewerTextPair_ = viewerTextPair_ == 10 ? 2 : viewerTextPair_ + 1;
    return;
  }
  if (alt && value == KEY_F(6)) {
    viewerBackground_ = (viewerBackground_ + 1) % 3;
    return;
  }
  if (alt && letter == 'p') {
    viewerPrint();
    return;
  }
  if (alt && (letter == 'v' || letter == 'e')) {
    const auto variable =
        letter == 'v' ? "LTREE_ALT_VIEWER" : "LTREE_ALT_EDITOR";
    auto command = qEnvironmentVariable(variable);
    if (command.isEmpty()) {
      status_ = QString("Set %1 to a terminal program").arg(variable);
      return;
    }
    shellCommand(command + " " + shellQuote(viewerPath_));
    return;
  }
  scrollSpeed_ = 0;
  if (value == 27 || value == 10 || value == 13 || value == KEY_ENTER ||
      letter == 'q') {
    viewer_ = false;
    continuous_ = false;
    scrollSpeed_ = 0;
    viewPaths_.clear();
    return;
  }
  if (value == KEY_F(1)) {
    helpDocument_.open(HelpTopic::Viewer);
    help_ = true;
    return;
  }
  const int page = rows_ - 5;
  if (!hex_ && viewerHighlight_ >= 0 &&
      (value == KEY_UP || value == KEY_DOWN)) {
    int delta = value == KEY_UP ? -1 : 1;
    viewerHighlight_ += delta;
    if (viewerHighlight_ < 0) {
      viewerHighlight_ = 0;
      viewerTop_ = std::max(0, viewerTop_ - 1);
    }
    if (viewerHighlight_ >= page) {
      viewerHighlight_ = page - 1;
      ++viewerTop_;
    }
    return;
  }
  if (alt && (value == KEY_HOME || value == KEY_END)) {
    if (viewPaths_.isEmpty())
      for (const auto &file : active_.session.files())
        viewPaths_ << file.path;
    viewSequenceIndex_ = value == KEY_HOME ? 0 : int(viewPaths_.size()) - 1;
    if (!viewPaths_.isEmpty())
      readDocument(viewPaths_[viewSequenceIndex_]);
    return;
  }
  int count = (hex_ || viewerMode_ == 1)
                  ? int((viewed_.bytes.size() + bytesPerRow(columns_) - 1) /
                        bytesPerRow(columns_))
                  : viewerLines_.size();
  if (value == 0x2305) {
    viewerHorizontal_ = 0;
    return;
  }
  if (value == 0x2306) {
    viewerHorizontal_ = 0;
    for (int row = viewerTop_;
         row < std::min(viewerTop_ + page, int(viewerLines_.size())); ++row)
      viewerHorizontal_ = std::max(
          viewerHorizontal_, int(viewerLines_[row].size()) - columns_ + 7);
    return;
  }
  if (letter == 'o') {
    viewerOffset();
  } else if (letter == 'e') {
    if (hex_ || terminalBinaryData(viewed_.bytes))
      beginHexEdit();
    else if (viewerSnapshot_.valid)
      editFile(viewerPath_);
    else
      status_ = "Extract the archive member before editing";
  } else if (letter == 'h') {
    hex_ = !hex_;
    viewerMode_ = 0;
    viewed_ = decodeBytes(viewed_.bytes, viewerCharset_);
    rebuildViewerLines();
    viewerTop_ = 0;
  } else if (letter == 'a' || letter == 'd' || letter == 'j') {
    hex_ = false;
    viewerMode_ = letter == 'a' ? 0 : letter == 'd' ? 1 : 2;
    viewed_ = decodeBytes(viewed_.bytes, viewerCharset_);
    viewerTop_ = 0;
    rebuildViewerLines();
  } else if (letter == 'c') {
    viewerCharset_ = (viewerCharset_ + (value == 'C' ? 6 : 1)) % 7;
    viewed_ = decodeBytes(viewed_.bytes, viewerCharset_);
    rebuildViewerLines();
    hit_ = -1;
  } else if (letter == 'w') {
    viewerWrap_ = !viewerWrap_;
    viewerHorizontal_ = 0;
    rebuildViewerLines();
  } else if (letter == 'r')
    viewerRuler_ = !viewerRuler_;
  else if (letter == 'l')
    viewerHighlight_ = viewerHighlight_ < 0 ? 0 : -1;
  else if (letter == 'm') {
    viewerMask_ = !viewerMask_;
    rebuildViewerLines();
    viewerTop_ = 0;
  } else if (value == 9) {
    const QVector<int> tabs{2, 3, 4, 6, 8};
    viewerTab_ = tabs[(tabs.indexOf(viewerTab_) + 1) % tabs.size()];
    rebuildViewerLines();
  } else if (letter >= '0' && letter <= '9')
    viewerOffset();
  else if (letter == 'g')
    viewerGather();
  else if (letter == 'x') {
    auto command = ask("EXECUTE command (empty opens shell): ");
    if (command)
      shellCommand(*command);
  } else if (value == KEY_F(3)) {
    if (viewerSnapshot_.valid)
      readDocument(viewerPath_);
    else
      status_ = "Archive member is already loaded";
  } else if (letter == 'f' || letter == 'b' || letter == 's' || value == '/' ||
             value == '\\' || value == KEY_F(9)) {
    if (searchPrompt(viewerSearch_)) {
      rememberedSearch_ = viewerSearch_;
      hit_ = -1;
      viewerFind(letter == 'b' || value == '\\', letter == 'f' || letter == 'b',
                 letter == 's');
    }
  } else if (value == ' ' || value == '+' || value == '-') {
    if (viewerHitSkip_)
      viewerFind(value == '-');
    else
      viewerTop_ += (value == '-' ? -page : page);
  } else if (value == KEY_F(8))
    viewerHitSkip_ = !viewerHitSkip_;
  else if (letter == 'n')
    sequenceFile(1);
  else if (letter == 'p')
    sequenceFile(-1);
  else if (letter == 'u')
    sequenceFile(1, true);
  else if (value == KEY_UP || value == 0x2301)
    --viewerTop_;
  else if (value == KEY_DOWN || value == 0x2302)
    ++viewerTop_;
  else if (value == KEY_PPAGE)
    viewerTop_ -= page;
  else if (value == KEY_NPAGE)
    viewerTop_ += page;
  else if (value == KEY_HOME || value == 0x2305) {
    viewerTop_ = 0;
    viewerHorizontal_ = 0;
  } else if (value == KEY_END || value == 0x2306)
    viewerTop_ = std::max(0, count - page);
  else if (value == KEY_LEFT || value == 0x2303)
    viewerHorizontal_ =
        std::max(0, viewerHorizontal_ - (value == 0x2303 ? 8 : 1));
  else if (value == KEY_RIGHT || value == 0x2304)
    viewerHorizontal_ += value == 0x2304 ? 8 : 1;
  else if (value == KEY_MOUSE) {
    MEVENT event;
    if (getmouse(&event) == OK) {
      if (event.bstate & BUTTON4_PRESSED)
        viewerTop_ -= 3;
#ifdef BUTTON5_PRESSED
      if (event.bstate & BUTTON5_PRESSED)
        viewerTop_ += 3;
#endif
    }
  }
  viewerTop_ = std::clamp(viewerTop_, 0, std::max(0, count - 1));
  if (!literal && value != KEY_RESIZE && value != KEY_MOUSE)
    scrollSpeed_ = 0;
}
