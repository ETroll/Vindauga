#include <QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>

#include "Settings/AppSettings.h"

using namespace vindauga;

// The feed/sign-in related settings: defaults, clamping, trimming and change signals.
class AppSettingsTest : public QObject {
    Q_OBJECT
private slots:
    void defaults() {
        QTemporaryDir dir;
        AppSettings settings(dir.path() + QStringLiteral("/settings.ini"));
        QCOMPARE(settings.preventSessionLockSeconds(), 0);
        QVERIFY(settings.feedDiscoveryUrl().isEmpty());
        QVERIFY(settings.signInUserAgent().isEmpty());
    }

    void preventSessionLockIsClampedAndSignalled() {
        QTemporaryDir dir;
        AppSettings settings(dir.path() + QStringLiteral("/settings.ini"));
        QSignalSpy spy(&settings, &AppSettings::preventSessionLockSecondsChanged);
        settings.setPreventSessionLockSeconds(180);
        QCOMPARE(settings.preventSessionLockSeconds(), 180);
        QCOMPARE(spy.count(), 1);
        settings.setPreventSessionLockSeconds(180); // unchanged: no signal
        QCOMPARE(spy.count(), 1);
        settings.setPreventSessionLockSeconds(99999);
        QCOMPARE(settings.preventSessionLockSeconds(), 3600);
        settings.setPreventSessionLockSeconds(-5);
        QCOMPARE(settings.preventSessionLockSeconds(), 0);
        QCOMPARE(spy.count(), 3);
    }

    void textSettingsAreTrimmedAndPersisted() {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/settings.ini");
        {
            AppSettings settings(path);
            QSignalSpy urlSpy(&settings, &AppSettings::feedDiscoveryUrlChanged);
            QSignalSpy uaSpy(&settings, &AppSettings::signInUserAgentChanged);
            settings.setFeedDiscoveryUrl(QStringLiteral("  https://rdweb.wvd.azure.us/api/arm/feeddiscovery "));
            settings.setSignInUserAgent(QStringLiteral(" Mozilla/5.0 (Windows NT 10.0) "));
            QCOMPARE(urlSpy.count(), 1);
            QCOMPARE(uaSpy.count(), 1);
            settings.setFeedDiscoveryUrl(QStringLiteral("https://rdweb.wvd.azure.us/api/arm/feeddiscovery"));
            QCOMPARE(urlSpy.count(), 1); // same value after trimming: no signal
        }
        AppSettings reloaded(path);
        QCOMPARE(reloaded.feedDiscoveryUrl(),
                 QStringLiteral("https://rdweb.wvd.azure.us/api/arm/feeddiscovery"));
        QCOMPARE(reloaded.signInUserAgent(), QStringLiteral("Mozilla/5.0 (Windows NT 10.0)"));
    }
};

QTEST_MAIN(AppSettingsTest)
#include "test_appsettings.moc"
