void transfer(bool move, bool tagged, bool paths) {
  const auto files = targets(tagged || paths);
  if (files.isEmpty()) {
    status_ = "No files selected";
    return;
  }
  CopyPaths pathMode = paths ? CopyPaths::Relative : CopyPaths::Flat;
  CopyCase letterCase = CopyCase::Keep;
  CopyReplace replace = CopyReplace::Ask;
  auto mask = ask(
      QString("%1 %2 file(s) as: ")
          .arg(move ? "MOVE" : "COPY")
          .arg(files.size()),
      "*.*", {},
      [&] {
        return QString("[Enter] Filename mask [Tab] Case (%1) [Esc] Cancel")
            .arg(QStringList{"Keep", "Lower", "Upper"}[int(letterCase)]);
      },
      [&](int k, QString &) {
        if (k != 9)
          return false;
        letterCase = CopyCase((int(letterCase) + 1) % 3);
        return true;
      });
  if (!mask)
    return;
  auto dest = ask("To: ", destination());
  if (!dest)
    return;
  if (paths) {
    int pick =
        choose("Duplicate paths",
               {"Full path", "Current directory", "Relative to branch", "Flat"},
               int(pathMode));
    if (pick < 0)
      return;
    pathMode = CopyPaths(pick);
  }
  int pick =
      choose("Replace existing files", {"Ask for each existing file", "Always",
                                        "Only older", "Never", "Rename"});
  if (pick < 0)
    return;
  replace = CopyReplace(pick);
  const auto plan = planCopy(files, active_.session.directory, absolute(*dest),
                             pathMode, *mask, letterCase);
  if (!plan.error.isEmpty()) {
    status_ = plan.error;
    return;
  }
  if (!confirm(QString("%1 %2 file(s) to %3?")
                   .arg(move ? "Move" : "Copy")
                   .arg(plan.entries.size())
                   .arg(absolute(*dest))))
    return;
  const bool each = tagged && confirm("Confirm copy/move for each file?");
  int done = 0, failed = 0, skipped = 0;
  QSet<QString> dirs;
  for (const auto &entry : plan.entries) {
    if (stopped)
      break;
    if (each && !confirm((move ? "Move " : "Copy ") + entry.source + "?")) {
      ++skipped;
      continue;
    }
    auto policy = replace;
    for (;;) {
      auto result = task(
          (move ? "Moving " : "Copying ") + entry.source,
          [=, root = operationRoot(entry.source)](const Cancellation &cancel) {
            return move ? moveFile(root, entry, policy, cancel)
                        : copyFile(root, entry, policy, cancel);
          });
      if (result.exists && policy == CopyReplace::Ask) {
        if (confirm("Replace " + entry.target + "?")) {
          policy = CopyReplace::Always;
          continue;
        }
        ++skipped;
        break;
      }
      if (result.cancelled) {
        status_ = "Transfer cancelled";
        refreshDirectories(dirs);
        return;
      }
      if (!result.error.isEmpty()) {
        status_ = result.error;
        int action = choose(result.error, {"Retry", "Skip", "Cancel"});
        if (action == 0)
          continue;
        ++failed;
        if (action != 1) {
          refreshDirectories(dirs);
          return;
        }
      } else if (result.copied || result.moved) {
        ++done;
        dirs.insert(QFileInfo(entry.source).absolutePath());
        dirs.insert(QFileInfo(result.target).absolutePath());
        if (move) {
          active_.session.removeFile(entry.source);
          if (other_)
            other_->session.removeFile(entry.source);
        }
        applyScan(result.scan);
      } else
        ++skipped;
      break;
    }
  }
  refreshDirectories(dirs);
  status_ = QString("%1: %2 completed; %3 skipped; %4 failed")
                .arg(move ? "Move" : "Copy")
                .arg(done)
                .arg(skipped)
                .arg(failed);
}
void removeItems(bool tagged) {
  auto &s = active_.session;
  const bool tree = s.view == View::Tree;
  const auto root = s.root, branch = s.directory;
  const auto view = s.view;
  const auto files = targets(tagged);
  if (!tree && files.isEmpty()) {
    status_ = "No files selected";
    return;
  }
  if (!confirm(tree ? "Delete empty directory " + branch + "?"
                    : QString("Delete %1 file(s)?").arg(files.size())))
    return;
  const bool each = tagged && confirm("Confirm delete for each file?");
  int done = 0, failed = 0, skipped = 0;
  QSet<QString> dirs;
  if (tree) {
    const auto result =
        task("Deleting " + branch, [=](const Cancellation &cancel) {
          return deleteEmptyDirectory(root, branch, cancel);
        });
    if (result.deleted) {
      s.removeDirectory(branch);
      if (other_)
        other_->session.removeDirectory(branch);
      applyScan(result.scan);
      status_ = "Directory deleted";
    } else
      status_ = result.error;
    return;
  }
  for (const auto &file : files) {
    if (each && !confirm("Delete " + file.path + "?")) {
      ++skipped;
      continue;
    }
    for (;;) {
      const auto result = task("Deleting " + file.path,
                               [file, fileRoot = operationRoot(file.path)](
                                   const Cancellation &cancel) {
                                 return deleteFile(fileRoot, file.path, cancel);
                               });
      if (result.deleted) {
        ++done;
        s.removeFile(file.path);
        if (other_)
          other_->session.removeFile(file.path);
        dirs.insert(QFileInfo(file.path).absolutePath());
        applyScan(result.scan);
        break;
      }
      if (result.cancelled) {
        refreshDirectories(dirs);
        status_ = "Delete cancelled";
        return;
      }
      status_ = result.error;
      int action = choose(result.error, {"Retry", "Skip", "Cancel"});
      if (action == 0)
        continue;
      ++failed;
      if (action != 1) {
        refreshDirectories(dirs);
        return;
      }
      break;
    }
  }
  refreshDirectories(dirs);
  if (tagged && view == View::Branch && failed == 0 && skipped == 0 &&
      done > 0 &&
      confirm("Delete empty branch directories and parent " + branch + "?")) {
    const auto result =
        task("Removing empty branch", [=](const Cancellation &cancel) {
          return deleteEmptyBranch(root, branch, cancel);
        });
    for (const auto &path : result.removedDirectories) {
      s.removeDirectory(path);
      if (other_)
        other_->session.removeDirectory(path);
    }
    applyScan(result.scan);
    if (!result.error.isEmpty()) {
      status_ = result.error;
      return;
    }
    s.returnToTree();
  }
  status_ = QString("Delete: %1 deleted; %2 skipped; %3 failed")
                .arg(done)
                .arg(skipped)
                .arg(failed);
}
void mkdirCommand() {
  auto name = ask("MAKE directory: ");
  if (!name)
    return;
  auto base = active_.session.directory;
  auto result = task("Creating directory", [=](const Cancellation &cancel) {
    return makeDirectory(base, *name, cancel);
  });
  applyScan(result.scan);
  if (result.error.isEmpty()) {
    active_.session.selectDirectory(result.path);
    status_ = "Directory created";
  } else
    status_ = result.error;
}
void renameCommand(bool tagged) {
  auto &s = active_.session;
  const bool tree = s.view == View::Tree;
  auto files = targets(tagged);
  if (!tree && files.isEmpty()) {
    status_ = "No files selected";
    return;
  }
  CopyCase letterCase = CopyCase::Keep;
  auto name = ask(
      "RENAME as: ",
      tree     ? QFileInfo(s.directory).fileName()
      : tagged ? "*.*"
               : files.first().name,
      {},
      [&] {
        return QString("[Enter] OK [Tab] Case (%1) [Esc] Cancel")
            .arg(QStringList{"Keep", "Lower", "Upper"}[int(letterCase)]);
      },
      [&](int k, QString &) {
        if (k != 9)
          return false;
        letterCase = CopyCase((int(letterCase) + 1) % 3);
        return true;
      });
  if (!name)
    return;
  QStringList paths;
  if (tree)
    paths << s.directory;
  else
    for (const auto &file : files)
      paths << file.path;
  if (!confirm(QString("Rename %1 item(s)?").arg(paths.size())))
    return;
  const bool each = tagged && confirm("Confirm rename for each file?");
  int done = 0, failed = 0;
  for (const auto &path : paths) {
    if (each && !confirm("Rename " + path + "?"))
      continue;
    QString error;
    auto target =
        copyName(QFileInfo(path).fileName(), *name, letterCase, &error);
    if (!error.isEmpty()) {
      status_ = error;
      ++failed;
      continue;
    }
    const auto expected = readMetadata(path);
    for (;;) {
      auto result = task("Renaming " + path, [=](const Cancellation &cancel) {
        return renameItem(expected, target, cancel);
      });
      if (result.renamed) {
        ++done;
        s.renamePath(result.source, result.target);
        if (other_)
          other_->session.renamePath(result.source, result.target);
        applyScan(result.scan);
        break;
      }
      if (result.cancelled) {
        status_ = "Rename cancelled";
        return;
      }
      status_ = result.error;
      int action = choose(result.error, {"Retry", "Skip", "Cancel"});
      if (action == 0)
        continue;
      ++failed;
      if (action != 1)
        return;
      break;
    }
  }
  if (failed == 0)
    status_ = QString("Rename: %1 completed").arg(done);
}
void shellCommand(const QString &command = {}) {
  auto executable = qEnvironmentVariable("SHELL", "/bin/bash");
  QStringList args;
  if (!command.isEmpty())
    args = {"-c", command};
  QProcess process;
  process.setWorkingDirectory(viewer_ ? QFileInfo(viewerPath_).absolutePath()
                                      : active_.session.directory);
  process.setProcessChannelMode(QProcess::ForwardedChannels);
  process.setInputChannelMode(QProcess::ForwardedInputChannel);
  process.setUnixProcessParameters(
      QProcess::UnixProcessFlag::ResetSignalHandlers);
  const auto prev = std::signal(SIGINT, SIG_IGN);
  keyboard_.suspend();
  def_prog_mode();
  endwin();
  process.start(executable, args);
  if (process.waitForStarted()) {
    while (process.state() != QProcess::NotRunning) {
      process.waitForFinished(100);
      QCoreApplication::processEvents();
      if (stopped) {
        process.terminate();
        if (!process.waitForFinished(3000))
          process.kill();
      }
    }
  }
  reset_prog_mode();
  refresh();
  keyboard_.resume();
  std::signal(SIGINT, prev);
  QSet<QString> dirs{active_.session.directory};
  if (other_)
    dirs.insert(other_->session.directory);
  refreshDirectories(dirs);
  status_ = process.error() == QProcess::FailedToStart ? process.errorString()
                                                       : "Shell finished";
}
void linkCommand() {
  auto source = readMetadata(active_.session.directory);
  auto dest =
      ask("SYMLINK destination: ",
          QDir(destination()).filePath(QFileInfo(source.path).fileName()));
  if (!dest)
    return;
  auto result =
      task("Creating symbolic link",
           [this, source, dest](const Cancellation &cancel) {
             return createDirectoryLink(source, absolute(*dest), cancel);
           });
  applyScan(result.scan);
  status_ = result.created ? "Symbolic link created" : result.error;
}
void graftCommand() {
  auto source = readMetadata(active_.session.directory);
  auto dest = ask("GRAFT branch to: ", destination(), "graft");
  if (!dest)
    return;
  auto target = readMetadata(absolute(*dest));
  if (!confirm("Move branch " + source.path + " to " + target.path + "?"))
    return;
  auto result = task("Moving branch", [=](const Cancellation &cancel) {
    return graftBranch(source, target, cancel);
  });
  if (result.moved) {
    active_.session.graftPath(result.source, result.target);
    if (other_)
      other_->session.graftPath(result.source, result.target);
    applyScan(result.scan);
    refreshDirectories({QFileInfo(source.path).absolutePath(), target.path});
    status_ = "Branch moved";
  } else
    status_ = result.error;
}
void pruneCommand() {
  PruneOptions options;
  auto root = active_.session.root;
  auto source = readMetadata(active_.session.directory);
  QMap<QString, FileMetadata> logged;
  for (auto it = active_.session.directories().cbegin();
       it != active_.session.directories().cend(); ++it)
    if (it->loaded)
      logged[it.key()] = readMetadata(it.key());
  auto word = ask(
      "Enter the word PRUNE: ", {}, {},
      [&] {
        return QString("[F5] Keep files (%1) [F4] Trash (%2) [F6] Only empty "
                       "(%3) [F2] Force read-only (%4) [Esc] Cancel")
            .arg(options.keepCurrentFiles ? "yes" : "no",
                 options.trash ? "yes" : "no", options.onlyEmpty ? "yes" : "no",
                 options.forceReadOnly ? "yes" : "no");
      },
      [&](int k, QString &) {
        if (k == KEY_F(5))
          options.keepCurrentFiles = !options.keepCurrentFiles;
        else if (k == KEY_F(4))
          options.trash = !options.trash;
        else if (k == KEY_F(6))
          options.onlyEmpty = !options.onlyEmpty;
        else if (k == KEY_F(2))
          options.forceReadOnly = !options.forceReadOnly;
        else
          return false;
        return true;
      });
  if (!word)
    return;
  if (*word != "PRUNE") {
    status_ = "PRUNE was not typed; no changes made";
    return;
  }
  auto result = task("Pruning " + source.path, [=](const Cancellation &cancel) {
    return pruneBranch(root, source, logged, options, cancel);
  });
  for (const auto &path : result.removedFiles) {
    active_.session.removeFile(path);
    if (other_)
      other_->session.removeFile(path);
  }
  for (const auto &path : result.removedDirectories) {
    active_.session.removeDirectory(path);
    if (other_)
      other_->session.removeDirectory(path);
  }
  applyScan(result.scan);
  status_ = QString("Prune: %1 files; %2 directories; %3 retained %4")
                .arg(result.removedFiles.size())
                .arg(result.removedDirectories.size())
                .arg(result.retained)
                .arg(result.error);
}
bool searchPrompt(SearchOptions &options) {
  auto query = ask(
      "SEARCH for: ", options.query, "search",
      [&] {
        return QString("[F2] Case sensitive (%1) [F4] Search for (%2) [Enter] "
                       "Search [↑↓] History [F3] Last [Esc] Cancel")
            .arg(options.caseSensitive ? "yes" : "no",
                 searchModeName(options.mode));
      },
      [&](int k, QString &) {
        if (k == KEY_F(2))
          options.caseSensitive = !options.caseSensitive;
        else if (k == KEY_F(4))
          options.mode = nextSearchMode(options.mode);
        else
          return false;
        return true;
      });
  if (!query)
    return false;
  options.query = *query;
  const auto error = validateSearch(options);
  if (!error.isEmpty()) {
    status_ = error;
    return false;
  }
  return true;
}
void batchSearch() {
  const auto files = targets(true);
  if (files.isEmpty()) {
    status_ = "No tagged files";
    return;
  }
  auto options = rememberedSearch_;
  if (!searchPrompt(options))
    return;
  rememberedSearch_ = options;
  cancel_ = std::make_shared<std::atomic_bool>(false);
  const auto cancel = cancel_;
  QElapsedTimer elapsed;
  elapsed.start();
  qint64 completed = 0, total = 0, actual = 0;
  int hits = 0, failed = 0, processed = 0;
  QMap<int, bool> policies;
  for (const auto &file : files)
    total += std::max(qint64(1), file.size);
  for (int index = 0; index < files.size() && !cancel->load(); ++index) {
    const auto file = files[index];
    for (int i = 0; i < active_.session.files().size(); ++i)
      if (active_.session.files()[i].path == file.path) {
        active_.session.fileIndex = i;
        break;
      }
    bool retry = true;
    while (retry && !cancel->load()) {
      retry = false;
      auto bytes = std::make_shared<std::atomic<qint64>>(0);
      auto future = QtConcurrent::run([=] {
        return searchFile(file.path, options, cancel,
                          [bytes](qint64 n) { bytes->store(n); });
      });
      while (!future.isFinished()) {
        qint64 read = bytes->load(),
               position =
                   completed + std::min(read, std::max(qint64(1), file.size));
        int percent = total ? std::min(100, int(position * 100 / total)) : 0;
        double seconds = std::max(0.001, elapsed.elapsed() / 1000.0),
               rate = (actual + read) / seconds;
        draw();
        for (int y = rows_ - 3; y < rows_; ++y)
          fill(0, y, columns_, 1);
        text(0, rows_ - 3,
             QString("Searching for (%1): %2  Hits: %3")
                 .arg(searchModeName(options.mode), options.query)
                 .arg(hits),
             2);
        text(0, rows_ - 2,
             QString("%1 in file %2/%3: %4")
                 .arg(QString("|/-\\")[spinner_++ % 4])
                 .arg(index + 1)
                 .arg(files.size())
                 .arg(file.path),
             1);
        text(0, rows_ - 1,
             QString("%1 %2% Time: %3s Left: %4s (%5 MiB/s) Esc Cancel")
                 .arg(QString(percent * std::min(20, columns_ / 6) / 100,
                              QChar(0x2588))
                          .leftJustified(std::min(20, columns_ / 6)))
                 .arg(percent)
                 .arg(int(seconds))
                 .arg(rate ? int(std::max(qint64(0), total - position) / rate)
                           : 0)
                 .arg(rate / 1048576, 0, 'f', 2),
             1);
        wrefresh(stdscr);
        int k = nextKey();
        if (k == 27 || k == 3)
          cancel->store(true);
      }
      auto result = future.result();
      actual += bytes->load();
      if (result.outcome == SearchOutcome::Cancelled) {
        cancel->store(true);
        break;
      }
      if (result.outcome == SearchOutcome::Found)
        ++hits;
      else if (result.outcome == SearchOutcome::NotFound)
        active_.session.tags.remove(file.path);
      else if (result.outcome == SearchOutcome::Error) {
        ++failed;
        bool keep = true;
        if (policies.contains(result.errorType))
          keep = policies[result.errorType];
        else {
          status_ = file.path + ": " + result.error;
          int action =
              choose(status_,
                     {"Retry", "Keep tag", "Untag", "Keep tags for this error",
                      "Untag for this error", "Cancel search"});
          if (action == 0) {
            retry = true;
            --failed;
            continue;
          }
          if (action == 5 || action < 0) {
            cancel->store(true);
            break;
          }
          keep = action == 1 || action == 3;
          if (action >= 3)
            policies[result.errorType] = keep;
        }
        if (!keep)
          active_.session.tags.remove(file.path);
      }
    }
    if (!cancel->load()) {
      ++processed;
      completed += std::max(qint64(1), file.size);
    }
  }
  active_.session.rebuild();
  status_ = QString("Search %1: %2/%3 processed; %4 hits; %5 errors")
                .arg(cancel->load() ? "cancelled" : "completed")
                .arg(processed)
                .arg(files.size())
                .arg(hits)
                .arg(failed);
}
