#include "terminal/keyboard.h"
#include <QtTest>
using namespace ltree;
class TerminalKeyboardTests : public QObject {
    Q_OBJECT
private slots:
    void events() {
        const auto press=decodeKeyboard("[97;5:1u");QVERIFY(press);QCOMPARE(press->code,97);QCOMPARE(press->modifiers,4);QCOMPARE(press->type,1);
        const auto release=decodeKeyboard("[57442;1:3u");QVERIFY(release);QCOMPARE(release->modifiers,0);QCOMPARE(release->type,3);
        const auto repeat=decodeKeyboard("[1;3:2B");QVERIFY(repeat);QCOMPARE(repeat->final,'B');QCOMPARE(repeat->type,2);
        const auto alternate=decodeKeyboard("[97:65:113;2;65u");QVERIFY(alternate);QCOMPARE(alternate->text,QVector<int>{65});
        const auto ime=decodeKeyboard("[0;1;233:128512u");QVERIFY(ime);QCOMPARE(ime->text,(QVector<int>{233,128512}));
        const auto defaults=decodeKeyboard("[9u");QVERIFY(defaults);QCOMPARE(defaults->type,1);QCOMPARE(defaults->modifiers,0);
        const auto reply=decodeKeyboard("[?0u");QVERIFY(reply);QVERIFY(reply->reply);
        QVERIFY(decodeKeyboard("[13;5~"));
        const auto shortArrow=decodeKeyboard("[B");QVERIFY(shortArrow);QCOMPARE(shortArrow->code,1);QCOMPARE(shortArrow->final,'B');
        QVERIFY(!decodeKeyboard("[u"));
    }
    void invalid_data() {
        QTest::addColumn<QByteArray>("input");
        const QList<QByteArray> values={"", "[", "[?64u", "[?1;2c", "[1;0u", "[1;257u", "[1;1:0u", "[1;1:4u", "[1;1:1:1u", "[1114112u", "[55296u", "[1;1;27u", "[1;1;127u", "[1;1;-1u", "[99999999999999u", "[1;1;65;66u", "[1:2:3:4u", "[1;foo:u", "[1uTAIL", QByteArray("[")+QByteArray(512,'1')+"u"};
        for (int n=0;n<values.size();++n) QTest::newRow(qPrintable(QString::number(n)))<<values[n];
    }
    void invalid() { QFETCH(QByteArray,input);QVERIFY(!decodeKeyboard(input)); }
};
QTEST_GUILESS_MAIN(TerminalKeyboardTests)
#include "test_terminal_keyboard.moc"
