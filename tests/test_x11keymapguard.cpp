#include <QtTest>

#include <QFileInfo>

#include "X11KeymapGuard.h"

using namespace vindauga;

namespace {
// A layout/variant name guaranteed not to exist in any xkeyboard-config. A real-world
// invalid variant such as "no(win)" could in principle appear in future xkb data.
const QByteArray kBogus = QByteArrayLiteral("vindauga_does_not_exist");

bool haveXkbData() {
    return QFileInfo::exists(QStringLiteral("/usr/share/X11/xkb/symbols/us"));
}
} // namespace

// Verifies the pure part of X11KeymapGuard (parse/serialize/compiles/repair) without
// an X server. apply() is only tested as a no-op without DISPLAY.
class X11KeymapGuardTest : public QObject {
    Q_OBJECT
private slots:
    void parseRoundtrip() {
        const QByteArray raw = QByteArrayLiteral("evdev\0pc105\0no\0win\0lv3:ralt_switch\0");
        const auto names = X11KeymapGuard::parseProperty(raw);
        QVERIFY(names.has_value());
        QCOMPARE(names->rules, QByteArray("evdev"));
        QCOMPARE(names->model, QByteArray("pc105"));
        QCOMPARE(names->layout, QByteArray("no"));
        QCOMPARE(names->variant, QByteArray("win"));
        QCOMPARE(names->options, QByteArray("lv3:ralt_switch"));
        QCOMPARE(X11KeymapGuard::serializeProperty(*names), raw);
    }

    void parseShortAndEmpty() {
        QVERIFY(!X11KeymapGuard::parseProperty(QByteArray()).has_value());
        // Only rules/model, no layout: nothing Chromium could fail on.
        QVERIFY(!X11KeymapGuard::parseProperty(QByteArrayLiteral("evdev\0pc105\0")).has_value());
        // A missing trailing NUL and missing fields are tolerated.
        const auto names = X11KeymapGuard::parseProperty(QByteArrayLiteral("evdev\0pc105\0us"));
        QVERIFY(names.has_value());
        QCOMPARE(names->layout, QByteArray("us"));
        QCOMPARE(names->variant, QByteArray());
        QCOMPARE(names->options, QByteArray());
    }

    void knownLayoutsCompile() {
        if (!haveXkbData())
            QSKIP("xkeyboard-config missing in /usr/share/X11/xkb");
        QVERIFY(X11KeymapGuard::groupCompiles("us", ""));
        QVERIFY(X11KeymapGuard::groupCompiles("no", ""));
        QVERIFY(X11KeymapGuard::groupCompiles("no", "winkeys"));
    }

    void bogusVariantDoesNotCompile() {
        if (!haveXkbData())
            QSKIP("xkeyboard-config missing in /usr/share/X11/xkb");
        QVERIFY(!X11KeymapGuard::groupCompiles("no", kBogus));
        QVERIFY(!X11KeymapGuard::groupCompiles(kBogus, ""));
    }

    void repairLeavesValidUntouched() {
        if (!haveXkbData())
            QSKIP("xkeyboard-config missing in /usr/share/X11/xkb");
        const XkbRulesNames names{"evdev", "pc105", "no,us", "winkeys,intl", "lv3:ralt_switch"};
        QVERIFY(X11KeymapGuard::compiles(names));
        const auto fixed = X11KeymapGuard::repair(names);
        QVERIFY(fixed.has_value());
        QCOMPARE(*fixed, names);
    }

    void repairDropsBogusVariant() {
        if (!haveXkbData())
            QSKIP("xkeyboard-config missing in /usr/share/X11/xkb");
        const XkbRulesNames names{"evdev", "pc105", "no", kBogus, "lv3:ralt_switch"};
        QVERIFY(!X11KeymapGuard::compiles(names));
        const auto fixed = X11KeymapGuard::repair(names);
        QVERIFY(fixed.has_value());
        QCOMPARE(fixed->layout, QByteArray("no"));
        QCOMPARE(fixed->variant, QByteArray());
        // Everything else is preserved; only what fails to compile is touched.
        QCOMPARE(fixed->rules, names.rules);
        QCOMPARE(fixed->model, names.model);
        QCOMPARE(fixed->options, names.options);
        QVERIFY(X11KeymapGuard::compiles(*fixed));
    }

    void repairIsPerGroup() {
        if (!haveXkbData())
            QSKIP("xkeyboard-config missing in /usr/share/X11/xkb");
        const XkbRulesNames names{"evdev", "pc105", "no,us", kBogus + ",intl", ""};
        const auto fixed = X11KeymapGuard::repair(names);
        QVERIFY(fixed.has_value());
        QCOMPARE(fixed->layout, QByteArray("no,us"));
        QCOMPARE(fixed->variant, QByteArray(",intl"));
        QVERIFY(X11KeymapGuard::compiles(*fixed));
    }

    void repairFallsBackToUsForBogusLayout() {
        if (!haveXkbData())
            QSKIP("xkeyboard-config missing in /usr/share/X11/xkb");
        const XkbRulesNames names{"evdev", "pc105", kBogus, "", ""};
        const auto fixed = X11KeymapGuard::repair(names);
        QVERIFY(fixed.has_value());
        QCOMPARE(fixed->layout, QByteArray("us"));
        QCOMPARE(fixed->variant, QByteArray());
    }

    void repairValidatesWholeListToo() {
        if (!haveXkbData())
            QSKIP("xkeyboard-config missing in /usr/share/X11/xkb");
        // Empty group plus a variant on the empty group. Whatever the repair produces
        // must compile both per group and as a whole.
        const XkbRulesNames names{"evdev", "pc105", "us,", ",de", ""};
        const auto fixed = X11KeymapGuard::repair(names);
        QVERIFY(fixed.has_value());
        QVERIFY(X11KeymapGuard::compiles(*fixed));
        QVERIFY(X11KeymapGuard::groupCompiles(fixed->layout, fixed->variant));
    }

    void applyIsNoopWithoutDisplay() {
        // apply() opens its own X connection from $DISPLAY (like Chromium); without
        // DISPLAY there is nothing to check. With DISPLAY set it would actually modify
        // the developer's X server, so only the no-op branch is tested.
        qunsetenv("DISPLAY");
        QVERIFY(!X11KeymapGuard::apply());
    }
};

QTEST_GUILESS_MAIN(X11KeymapGuardTest)
#include "test_x11keymapguard.moc"
