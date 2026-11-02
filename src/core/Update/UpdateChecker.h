#pragma once
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Looks for a newer Vindauga release on GitHub. One GET of the "latest release" API
// endpoint answers everything needed: the version, the release notes and, per asset, the
// download URL and the SHA-256 digest GitHub computes at upload. Pure core/ (QtCore +
// QtNetwork); scheduling, backoff and UI live in UpdateController.
namespace vindauga {

// Which of our two release packages fits this system.
enum class PackageKind { Unknown, Arch, Deb };

struct ReleaseInfo {
    QString version;   // "1.5.3" (no leading "v")
    QString notes;     // release body (markdown), may be empty
    QUrl pageUrl;      // the release's web page
    QString assetName; // empty when there is no package for this system
    QUrl assetUrl;
    QString sha256;    // lower-case hex, 64 chars; empty when GitHub gave no digest
    qint64 size = 0;

    // A package can be installed automatically only with a trusted URL and a digest to
    // verify it against; otherwise the user is sent to the release page.
    bool installable() const { return !assetUrl.isEmpty() && sha256.size() == 64; }
};

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    struct ParseResult {
        bool ok = false;
        QString error;
        ReleaseInfo release;
    };

    struct Result {
        bool ok = false;
        QString error;
        ReleaseInfo release; // valid when ok
        // Failure only: seconds GitHub asked us to wait (Retry-After, or until
        // X-RateLimit-Reset when the quota is used up); 0 = no hint.
        qint64 retryAfterSecs = 0;
    };

    // apiUrl defaults to VINDAUGA_UPDATE_API_URL or the GitHub "latest release" endpoint.
    explicit UpdateChecker(QUrl apiUrl = defaultApiUrl(), QObject* parent = nullptr);
    ~UpdateChecker() override;

    static QUrl defaultApiUrl();

    // "v1.5.3" / "1.5.3" -> 1.5.3; null QVersionNumber-equivalent (empty) when it does not
    // start with a number. Pre-release suffixes ("-rc1") are ignored.
    static QList<int> parseVersion(const QString& text);
    // True only if `candidate` is strictly newer than `current`; false if either is
    // unparseable (never offer an update on a guess).
    static bool isNewer(const QString& candidate, const QString& current);

    // Arch when pacman is installed, Deb when dpkg is, on x86_64 only (the only
    // architecture we publish); Unknown otherwise.
    static PackageKind detectPackageKind();

    // https://github.com/<repo>/releases/download/... only. The asset URL comes from a
    // JSON document, and the file it names is later installed as root, so it is checked
    // before use. (The digest, not the host, protects the content; this keeps a bogus
    // document from pointing us at an arbitrary server at all.)
    static bool isTrustedAssetUrl(const QUrl& url);

    static ParseResult parseRelease(const QByteArray& json, PackageKind kind);

    // CHANGELOG.md at the release's tag (raw.githubusercontent.com, not the rate-limited
    // API). VINDAUGA_UPDATE_CHANGELOG_URL overrides it (tests, debugging).
    static QUrl changelogUrl(const QString& version);
    // The sections of a CHANGELOG.md ("## 1.5.3 — date" headings) for the versions the
    // user would get: newer than `current`, up to and including `latest`. The latest is
    // always included, so a same-version debug run still has something to show. With more
    // than one section each keeps its heading. Empty when nothing matches.
    static QString changelogSince(const QString& changelog, const QString& current,
                                  const QString& latest);

    // Starts one request; finished() is emitted exactly once. No-op while one is running.
    void check(PackageKind kind);
    void abort();
    bool running() const { return !m_reply.isNull(); }

    // Fetches the changelog text for `latest` (only worth doing once an update is known
    // to exist). changelogFetched() is emitted once; text is empty on any failure, in
    // which case the caller keeps the release body.
    void fetchChangelog(const QString& latest, const QString& current);

signals:
    void finished(const vindauga::UpdateChecker::Result& result);
    void changelogFetched(const QString& version, const QString& text);

private:
    void onReplyFinished();

    QUrl m_apiUrl;
    QNetworkAccessManager* m_nam;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_notesReply;
    PackageKind m_kind = PackageKind::Unknown;
};

} // namespace vindauga
