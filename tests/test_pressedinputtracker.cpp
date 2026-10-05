#include <QtTest>

#include "Rdp/PressedInputTracker.h"

using namespace vindauga;

class PressedInputTrackerTest : public QObject {
    Q_OBJECT
private slots:
    void emptyTrackerReleasesNothing() {
        PressedInputTracker tracker;
        QVERIFY(tracker.takeAll().isEmpty());
    }

    void releasedKeyIsNotReleasedAgain() {
        PressedInputTracker tracker;
        tracker.keyDown(0x1F);
        tracker.keyUp(0x1F);
        QVERIFY(tracker.takeAll().isEmpty());
    }

    void heldKeysAreReleasedOnTakeAll() {
        PressedInputTracker tracker;
        tracker.keyDown(0x5B); // Super
        tracker.keyDown(0x18); // O
        tracker.keyUp(0x18);
        tracker.keyDown(0x2A); // Shift

        const auto releases = tracker.takeAll();
        QCOMPARE(releases.keys.size(), 2);
        QVERIFY(releases.keys.contains(0x5B));
        QVERIFY(releases.keys.contains(0x2A));
        QVERIFY(releases.buttons.isEmpty());
    }

    void repeatedPressIsReleasedOnce() {
        PressedInputTracker tracker;
        tracker.keyDown(0x1E);
        tracker.keyDown(0x1E); // auto-repeat
        const auto releases = tracker.takeAll();
        QCOMPARE(releases.keys, QList<quint32>{0x1E});
    }

    void takeAllForgetsEverything() {
        PressedInputTracker tracker;
        tracker.keyDown(0x1E);
        tracker.buttonDown(Qt::LeftButton);
        QVERIFY(!tracker.takeAll().isEmpty());
        QVERIFY(tracker.takeAll().isEmpty());
    }

    void heldButtonsAreReleasedOnTakeAll() {
        PressedInputTracker tracker;
        tracker.buttonDown(Qt::LeftButton);
        tracker.buttonDown(Qt::RightButton);
        tracker.buttonUp(Qt::RightButton);

        const auto releases = tracker.takeAll();
        QCOMPARE(releases.buttons, QList<Qt::MouseButton>{Qt::LeftButton});
        QVERIFY(releases.keys.isEmpty());
    }

    void clearDropsHeldInputWithoutReturningIt() {
        PressedInputTracker tracker;
        tracker.keyDown(0x1E);
        tracker.buttonDown(Qt::LeftButton);
        tracker.clear();
        QVERIFY(tracker.takeAll().isEmpty());
    }
};

QTEST_APPLESS_MAIN(PressedInputTrackerTest)
#include "test_pressedinputtracker.moc"
