#pragma once
#include <QStringList>
#include <QVector>

namespace ltree {
enum class HelpTopic { Display, Navigation, Tags, Filespec, Search, Transfer, Delete,
                       Metadata, Viewer, Archives, Compare, Console, Prune, Graft, Stamp, Statistics, Terminal, TerminalFiles, TerminalView };
enum class HelpStyle { Text, Heading, Keys, Diagram };
struct HelpLine { QString text; HelpStyle style = HelpStyle::Text; int keyLength = 0; };
struct HelpSection { HelpTopic id; QString title, body; };
enum class HelpAction { Up, Down, PageUp, PageDown, Home, End, Previous, Next,
                        Index, Accept, Close, Find, Repeat, Backspace, Input };

class HelpDocument {
public:
    explicit HelpDocument(bool terminal = false);
    void open(HelpTopic topic);
    bool act(HelpAction action, int width, int height, const QString &text = {});
    QVector<HelpLine> lines(int width) const;
    void constrain(int width, int height);
    const QVector<HelpSection> &sections() const { return sections_; }
    int section() const { return section_; }
    int selected() const { return selected_; }
    int top() const { return top_; }
    bool index() const { return section_ < 0; }
    bool finding() const { return finding_; }
    QString title() const;
    QString query() const { return query_; }
    QString notice() const { return notice_; }
    int matchLine() const { return match_; }
private:
    QVector<HelpSection> sections_;
    int section_ = 0, selected_ = 0, top_ = 0, match_ = -1;
    bool finding_ = false;
    QString query_, draft_, notice_;
    void find(int width, int height, bool next);
};
}
