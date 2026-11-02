#include <QtTest>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "Update/UpdateChecker.h"
#include "Update/UpdateDownloader.h"
#include "Update/UpdateInstaller.h"

using namespace vindauga;

Q_DECLARE_METATYPE(vindauga::UpdateChecker::Result)
Q_DECLARE_METATYPE(vindauga::UpdateDownloader::Outcome)

namespace {

const QString kDigest =
    QStringLiteral("sha256:270d3d8edd54f07bcb57e9842d790ba33f909853b3b50200014b9b2b8c31e8ec");

QJsonObject asset(const QString& name, const QString& digest = kDigest,
                  const QString& url = QString()) {
    return QJsonObject{
        {"name", name},
        {"digest", digest},
        {"size", 584301},
        {"browser_download_url",
         url.isEmpty() ? QStringLiteral("https://github.com/ETroll/Vindauga/releases/download/v1.5.3/") + name
                       : url},
    };
}

QByteArray releaseJson(const QJsonArray& assets, const QString& tag = QStringLiteral("v1.5.3")) {
    return QJsonDocument(QJsonObject{
                             {"tag_name", tag},
                             {"body", "Notes"},
                             {"html_url", "https://github.com/ETroll/Vindauga/releases/tag/v1.5.3"},
                             {"assets", assets},
                         })
        .toJson();
}

// A one-shot-per-connection HTTP server on localhost for the network classes.
class FakeServer : public QTcpServer {
public:
    QByteArray status = "200 OK";
    QByteArray extraHeaders;
    QByteArray body;
    int requests = 0;
    FakeServer() {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            QTcpSocket* s = nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [this, s]() {
                s->readAll();
                ++requests;
                s->write("HTTP/1.1 " + status + "\r\nContent-Length: " +
                         QByteArray::number(body.size()) + "\r\n" + extraHeaders +
                         "Connection: close\r\n\r\n" + body);
                s->disconnectFromHost();
            });
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl url(const QString& path = QStringLiteral("/")) const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));
    }
};

} // namespace

class UpdateTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<UpdateChecker::Result>();
        qRegisterMetaType<UpdateDownloader::Outcome>();
    }

    void versionComparison() {
        QVERIFY(UpdateChecker::isNewer("v1.5.3", "1.5.2"));
        QVERIFY(UpdateChecker::isNewer("1.10.0", "1.9.9")); // numeric, not textual
        QVERIFY(UpdateChecker::isNewer("2.0", "1.9.9"));
        QVERIFY(!UpdateChecker::isNewer("1.5.2", "1.5.2"));
        QVERIFY(!UpdateChecker::isNewer("1.5", "1.5.0"));
        QVERIFY(!UpdateChecker::isNewer("1.5.1", "1.5.2")); // never offer a downgrade
        QVERIFY(!UpdateChecker::isNewer("nightly", "1.5.2"));
        QVERIFY(!UpdateChecker::isNewer("1.6.0", "")); // unknown current version: no guess
    }

    void trustedAssetUrl() {
        QVERIFY(UpdateChecker::isTrustedAssetUrl(
            QUrl("https://github.com/ETroll/Vindauga/releases/download/v1.5.3/x.deb")));
        QVERIFY(!UpdateChecker::isTrustedAssetUrl(
            QUrl("http://github.com/ETroll/Vindauga/releases/download/v1.5.3/x.deb")));
        QVERIFY(!UpdateChecker::isTrustedAssetUrl(
            QUrl("https://github.com.evil.io/ETroll/Vindauga/releases/download/v1.5.3/x.deb")));
        QVERIFY(!UpdateChecker::isTrustedAssetUrl(
            QUrl("https://github.com@evil.io/ETroll/Vindauga/releases/download/v1.5.3/x.deb")));
        QVERIFY(!UpdateChecker::isTrustedAssetUrl(
            QUrl("https://github.com/Other/Repo/releases/download/v1.5.3/x.deb")));
    }

    void picksTheRightAssetPerPackageKind() {
        const QByteArray json = releaseJson({
            asset("vindauga-debug-1.5.3-1-x86_64.pkg.tar.zst"),
            asset("vindauga-1.5.3-1-x86_64.pkg.tar.zst"),
            asset("vindauga_1.5.3_amd64.deb", "sha256:" + QString(64, QLatin1Char('a'))),
        });
        auto arch = UpdateChecker::parseRelease(json, PackageKind::Arch);
        QVERIFY(arch.ok);
        QCOMPARE(arch.release.version, QString("1.5.3"));
        QCOMPARE(arch.release.assetName, QString("vindauga-1.5.3-1-x86_64.pkg.tar.zst")); // not -debug-
        QVERIFY(arch.release.installable());
        QCOMPARE(arch.release.sha256, kDigest.mid(7));
        QCOMPARE(arch.release.pageUrl.host(), QString("github.com"));

        auto deb = UpdateChecker::parseRelease(json, PackageKind::Deb);
        QCOMPARE(deb.release.assetName, QString("vindauga_1.5.3_amd64.deb"));

        auto unknown = UpdateChecker::parseRelease(json, PackageKind::Unknown);
        QVERIFY(unknown.ok);
        QVERIFY(!unknown.release.installable()); // version known, nothing to install
    }

    void releaseWithoutUsableDigestOrUrlIsNotInstallable() {
        auto noDigest = UpdateChecker::parseRelease(
            releaseJson({asset("vindauga_1.5.3_amd64.deb", QString())}), PackageKind::Deb);
        QVERIFY(noDigest.ok);
        QVERIFY(!noDigest.release.installable());

        auto badDigest = UpdateChecker::parseRelease(
            releaseJson({asset("vindauga_1.5.3_amd64.deb", "sha256:xyz")}), PackageKind::Deb);
        QVERIFY(!badDigest.release.installable());

        auto evilUrl = UpdateChecker::parseRelease(
            releaseJson({asset("vindauga_1.5.3_amd64.deb", kDigest, "https://evil.io/x.deb")}),
            PackageKind::Deb);
        QVERIFY(evilUrl.ok);
        QVERIFY(evilUrl.release.assetUrl.isEmpty());
    }

    void rejectsGarbageAndPrereleases() {
        QVERIFY(!UpdateChecker::parseRelease("not json", PackageKind::Deb).ok);
        QVERIFY(!UpdateChecker::parseRelease(releaseJson({}, "nightly"), PackageKind::Deb).ok);
        QJsonObject pre{{"tag_name", "v9.0.0"}, {"prerelease", true}};
        QVERIFY(!UpdateChecker::parseRelease(QJsonDocument(pre).toJson(), PackageKind::Deb).ok);
    }

    void changelogSections() {
        const QString log = QStringLiteral(
            "# Changelog\n\nintro\n\n"
            "## Unreleased\n\n- not yet\n\n"
            "## 1.5.3 — 2026-11-10\n\n- three\n\n"
            "## 1.5.2 — 2026-11-02\n\n- two\n\n"
            "## 1.5.1 — 2026-10-30\n\n- one\n");
        // Skipping a version: both newer sections, newest first, with headings.
        const QString both = UpdateChecker::changelogSince(log, "1.5.1", "1.5.3");
        QVERIFY(both.contains("### 1.5.3 — 2026-11-10"));
        QVERIFY(both.contains("- two"));
        QVERIFY(!both.contains("- one"));
        QVERIFY(!both.contains("not yet"));
        // One step: just the text, no heading.
        QCOMPARE(UpdateChecker::changelogSince(log, "1.5.2", "1.5.3"), QString("- three"));
        // Same version (debug run): the latest section is still shown.
        QCOMPARE(UpdateChecker::changelogSince(log, "1.5.2", "1.5.2"), QString("- two"));
        QVERIFY(UpdateChecker::changelogSince(log, "1.5.2", "9.9.9").isEmpty() == false); // 1.5.3 > 1.5.2, <= 9.9.9
        QVERIFY(UpdateChecker::changelogSince("no headings", "1.0", "1.1").isEmpty());
    }

    void checkerFetchesTheChangelog() {
        FakeServer server;
        server.body = "## 1.5.3 \xe2\x80\x94 2026-11-10\n\n- three\n\n## 1.5.2 \xe2\x80\x94 x\n\n- two\n";
        qputenv("VINDAUGA_UPDATE_CHANGELOG_URL", server.url().toEncoded());
        UpdateChecker checker(server.url());
        QSignalSpy spy(&checker, &UpdateChecker::changelogFetched);
        checker.fetchChangelog("1.5.3", "1.5.2");
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toString(), QString("1.5.3"));
        QCOMPARE(spy.at(0).at(1).toString(), QString("- three"));
        // Failure -> empty text, caller keeps the release body.
        server.status = "404 Not Found";
        spy.clear();
        checker.fetchChangelog("1.5.3", "1.5.2");
        QVERIFY(spy.wait(5000));
        QVERIFY(spy.at(0).at(1).toString().isEmpty());
        qunsetenv("VINDAUGA_UPDATE_CHANGELOG_URL");
    }

    void installerCommands() {
        const QStringList args =
            UpdateInstaller::pkexecArguments(PackageKind::Arch, "/home/u/a b.pkg", "abc");
        QCOMPARE(args.at(0), QString("/bin/sh"));
        QVERIFY(args.at(2).contains("sha256sum -c"));
        QVERIFY(args.at(2).contains("pacman -U --noconfirm"));
        QCOMPARE(args.mid(3), (QStringList{"sh", "/home/u/a b.pkg", "vindauga.pkg.tar.zst", "abc"}));
        // The package path is an argument, never spliced into the script text.
        QVERIFY(!args.at(2).contains("a b.pkg"));

        const QStringList deb = UpdateInstaller::pkexecArguments(PackageKind::Deb, "/x.deb", "abc");
        QVERIFY(deb.at(2).contains("apt-get install -y"));
        QVERIFY(deb.at(2).contains("DEBIAN_FRONTEND=noninteractive"));

        QCOMPARE(UpdateInstaller::manualCommand(PackageKind::Arch, "/c/it's/p.pkg.tar.zst"),
                 QString("sudo pacman -U '/c/it'\\''s/p.pkg.tar.zst'"));
    }

    void checkerReadsTheReleaseFromTheServer() {
        FakeServer server;
        server.body = releaseJson({asset("vindauga_1.5.3_amd64.deb")});
        UpdateChecker checker(server.url());
        QSignalSpy spy(&checker, &UpdateChecker::finished);
        checker.check(PackageKind::Deb);
        QVERIFY(spy.wait(5000));
        const auto r = spy.takeFirst().at(0).value<UpdateChecker::Result>();
        QVERIFY(r.ok);
        QCOMPARE(r.release.version, QString("1.5.3"));
        QVERIFY(r.release.installable());
    }

    void checkerHonoursRateLimitHints() {
        FakeServer server;
        server.status = "403 Forbidden";
        server.extraHeaders = "Retry-After: 600\r\n";
        UpdateChecker checker(server.url());
        QSignalSpy spy(&checker, &UpdateChecker::finished);
        checker.check(PackageKind::Deb);
        QVERIFY(spy.wait(5000));
        const auto r = spy.takeFirst().at(0).value<UpdateChecker::Result>();
        QVERIFY(!r.ok);
        QCOMPARE(r.retryAfterSecs, 600);
    }

    void downloaderAcceptsMatchingChecksum() {
        FakeServer server;
        server.body = QByteArray(300000, 'x');
        QTemporaryDir dir;
        const QString dest = dir.filePath("p.deb");
        UpdateDownloader dl;
        QSignalSpy spy(&dl, &UpdateDownloader::finished);
        dl.start(server.url(), dest,
                 QString::fromLatin1(QCryptographicHash::hash(server.body, QCryptographicHash::Sha256).toHex()),
                 server.body.size());
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.takeFirst().at(0).value<UpdateDownloader::Outcome>(), UpdateDownloader::Outcome::Ok);
        QFile f(dest);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), server.body);
    }

    void downloaderRejectsAndDeletesOnChecksumMismatch() {
        FakeServer server;
        server.body = QByteArray(1000, 'x');
        QTemporaryDir dir;
        const QString dest = dir.filePath("p.deb");
        UpdateDownloader dl;
        QSignalSpy spy(&dl, &UpdateDownloader::finished);
        dl.start(server.url(), dest, QString(64, QLatin1Char('0')), server.body.size());
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.takeFirst().at(0).value<UpdateDownloader::Outcome>(), UpdateDownloader::Outcome::Failed);
        QVERIFY(!QFile::exists(dest));
    }

    void downloaderFailsOnHttpError() {
        FakeServer server;
        server.status = "404 Not Found";
        QTemporaryDir dir;
        const QString dest = dir.filePath("p.deb");
        UpdateDownloader dl;
        QSignalSpy spy(&dl, &UpdateDownloader::finished);
        dl.start(server.url(), dest, QString(64, QLatin1Char('0')), 0);
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.takeFirst().at(0).value<UpdateDownloader::Outcome>(), UpdateDownloader::Outcome::Failed);
        QVERIFY(!QFile::exists(dest));
    }
};

QTEST_MAIN(UpdateTest)
#include "test_update.moc"
