void sortCommand() {
  auto &s = active_.session;
  StringList names{"Name",
                    "Extension",
                    "Date",
                    "Time",
                    "Length",
                    "Alpha",
                    "Number",
                    "Size",
                    "Unsorted",
                    "Value",
                    "Toggle direction",
                    "Toggle path first",
                    "Apply to both panels",
                    "Reset"};
  while (true) {
    int pick = choose("SORT: " + sortLabel(s.sort), names, int(s.sort.key));
    if (pick < 0)
      return;
    if (pick < 10)
      s.sort.key = SortKey(pick);
    else if (pick == 10)
      s.sort.descending = !s.sort.descending;
    else if (pick == 11)
      s.sort.pathFirst = !s.sort.pathFirst;
    else if (pick == 12) {
      if (other_) {
        other_->session.sort = s.sort;
        other_->session.rebuild();
      }
    } else
      s.sort = {};
    s.rebuild();
    if (pick < 10 || pick == 13)
      return;
  }
}
void globalCommand(bool tagged) {
  roots_.insert(active_.session.root, active_);
  Vector<Session> sessions;
  for (auto it = roots_.cbegin(); it != roots_.cend(); ++it)
    sessions << it->session;
  if (other_)
    sessions << other_->session;
  if (!globalReturn_)
    globalReturn_ = active_;
  active_.session = Session::global(sessions, active_.session, tagged);
  active_.fileTop = 0;
}
void cycleRoot(int direction) {
  roots_.insert(active_.session.root, active_);
  StringList paths = roots_.keys();
  if (other_ && !paths.contains(other_->session.root)) {
    roots_.insert(other_->session.root, *other_);
    paths = roots_.keys();
  }
  if (paths.size() < 2) {
    status_ = "Only one logged root; L logs another location";
    return;
  }
  int i = paths.indexOf(active_.session.root);
  stopAutoview();
  active_ = roots_[paths[(i + direction + paths.size()) % paths.size()]];
}
void availability() {
  locations_ = mountedLocations();
  location_ = true;
  avail_ = true;
  locationIndex_ = locationTop_ = 0;
  auto stats = systemStatistics(active_.session.directory);
  for (int i = 0; i < locations_.size(); ++i)
    if (locations_[i].path == stats.mount)
      locationIndex_ = i;
  status_ = String("%1 %2  Capacity %3 bytes  Available %4 bytes")
                .arg(stats.mount, stats.filesystem)
                .arg(stats.capacity)
                .arg(stats.available);
}

void statsCommand() {
  int top = 0;
  while (!stopped) {
    auto system = systemStatistics(active_.session.directory);
    auto logged = loggedStatistics(active_.session);
    StringList lines{
        "DISK STATISTICS                           LOGGED FILE STATISTICS",
        "Mount: " + system.mount,
        "Source: " + system.source,
        "File system: " + system.filesystem,
        String(
            "Capacity: %1 bytes                   Total files: %2 / %3 bytes")
            .arg(system.capacity)
            .arg(logged.totalFiles)
            .arg(logged.totalBytes),
        String("Available: %1 bytes                  Matching: %2 / %3 bytes")
            .arg(system.available)
            .arg(logged.matchingFiles)
            .arg(logged.matchingBytes),
        String("Used: %1 bytes                       Tagged: %2 / %3 bytes")
            .arg(system.capacity - system.free)
            .arg(logged.taggedFiles)
            .arg(logged.taggedBytes),
        String("Block size: %1 bytes                 Directories logged: %2")
            .arg(system.blockSize)
            .arg(logged.directories),
        String("Total blocks: %1                     Displayed: %2 / %3 bytes")
            .arg(system.totalBlocks)
            .arg(logged.displayedFiles)
            .arg(logged.displayedBytes),
        "",
        "SYSTEM INFORMATION                       DISPLAY INFORMATION",
        String("RAM total: %1 MiB                    Cols %2 Rows %3")
            .arg(system.ramTotal / 1048576)
            .arg(columns_)
            .arg(rows_),
        String("RAM available: %1 MiB                List height %2")
            .arg(system.ramAvailable / 1048576)
            .arg(end() - 2),
        String("Committed: %1 MiB / %2 MiB")
            .arg(system.committed / 1048576)
            .arg(system.commitLimit / 1048576),
        String("CPU speed: %1 MHz").arg(system.cpuMHz),
        "CPU type: " + system.cpuType,
        "",
        "LOGON INFORMATION",
        "Computer name: " + system.host,
        "User login name: " + system.user,
        "Session: " + system.sessionType + " / " + system.desktop};
    draw();
    for (int y = 1; y < rows_ - 3; ++y)
      fill(1, y, columns_ - 2, 1);
    text(2, 1,
         "LTree Commander / Statistics " +
             system.measured.toString("yyyy-MM-dd HH:mm:ss"),
         2);
    int h = rows_ - 6;
    top = std::clamp(top, 0, std::max(0, int(lines.size()) - h));
    for (int i = 0; i < h && top + i < lines.size(); ++i)
      text(2, 3 + i, lines[top + i],
           lines[top + i].contains("INFORMATION") ||
                   lines[top + i].contains("STATISTICS")
               ? 2
               : 1,
           columns_ - 4);
    fill(0, rows_ - 2, columns_, 1);
    commands(0, rows_ - 2,
             "[↑↓/PgUp/PgDn] Scroll [F3] Refresh [Enter/Esc] Return");
    wrefresh(stdscr);
    int k = nextKey();
    if (k == 27 || k == 10 || k == 13 || k == KEY_ENTER)
      return;
    if (k == KEY_UP)
      --top;
    else if (k == KEY_DOWN)
      ++top;
    else if (k == KEY_PPAGE)
      top -= h;
    else if (k == KEY_NPAGE)
      top += h;
  }
}
void drawInfoBox() {
  auto &s = active_.session;
  auto metadata = readMetadata(s.view == View::Tree || !s.currentFile()
                                   ? s.directory
                                   : s.currentFile()->path);
  int w = std::min(51, columns_ - 4), x = columns_ - w - 2,
      y = std::max(3, end() - 12);
  StringList lines{
      "FILE INFORMATION",
      FileInfo(metadata.path).fileName(),
      "Owner: " + metadata.owner + "  Group: " + metadata.group,
      "Permissions: " + permissionText(metadata.mode),
      "Size: " + String::number(metadata.size) + " bytes",
      "Written: " + metadata.modified.toString("yyyy-MM-dd HH:mm:ss"),
      "Accessed: " + metadata.accessed.toString("yyyy-MM-dd HH:mm:ss"),
      "Changed: " + metadata.changed.toString("yyyy-MM-dd HH:mm:ss"),
      "Created: " + metadata.created.toString("yyyy-MM-dd HH:mm:ss"),
      String("Inode: %1  Links: %2").arg(metadata.inode).arg(metadata.links),
      metadata.linkTarget.isEmpty() ? metadata.type
                                    : "Link: " + metadata.linkTarget};
  for (int i = 0; i < lines.size() && y + i < end(); ++i) {
    fill(x, y + i, w, 1);
    text(x, y + i, lines[i], i == 0 ? 2 : 1, w);
  }
}
