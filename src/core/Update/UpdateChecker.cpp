#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

#include <algorithm>

#include "Logging.h"

namespace vindauga {

namespace {

constexpr auto kRepo = "ETroll/Vindauga";
constexpr qint64 kMaxRetryAfterSecs = 24 * 3600;

// Our release assets: vindauga-1.5.2-1-x86_64.pkg.tar.zst (Arch; the "-debug-" package
// next to it must not match) and vindauga_1.5.2_amd64.deb.
const QRegularExpression& archAssetPattern() {
    static const QRegularExpression re(
        QStringLiteral("^vindauga-\\d+(\\.\\d+)*-\\d+-x86_64\\.pkg\\.tar\\.zst$"));
    return re;
}
const QRegularExpression& debAssetPattern() {
    static const QRegularExpression re(QStringLiteral("^vindauga_\\d+(\\.\\d+)*_amd64\\.deb$"));
    return re;
}

QString normalizedSha256(QString digest) {
    digest = digest.trimmed().toLower();
    if (!digest.startsWith(QStringLiteral("sha256:")))
        return {};
    digest = digest.mid(7);
    static const QRegularExpression hex64(QStringLiteral("^[0-9a-f]{64}$"));
    return hex64.match(digest).hasMatch() ? digest : QString();
}

} // namespace

UpdateChecker::UpdateChecker(QUrl apiUrl, QObject* parent)
    : QObject(parent), m_apiUrl(std::move(apiUrl)), m_nam(new QNetworkAccessManager(this)) {}

UpdateChecker::~UpdateChecker() {
    abort();
}

QUrl UpdateChecker::defaultApiUrl() {
    const QByteArray env = qgetenv("VINDAUGA_UPDATE_API_URL");
    if (!env.isEmpty())
        return QUrl::fromEncoded(env);
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                    .arg(QLatin1String(kRepo)));
}

QList<int> UpdateChecker::parseVersion(const QString& text) {
    static const QRegularExpression re(QStringLiteral("^v?(\\d+(?:\\.\\d+)*)"));
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch())
        return {};
    QList<int> parts;
    for (const QString& p : m.captured(1).split(QLatin1Char('.'))) {
        bool ok = false;
        const int n = p.toInt(&ok);
        if (!ok)
            return {}; // overflow
        parts.append(n);
    }
    return parts;
}

bool UpdateChecker::isNewer(const QString& candidate, const QString& current) {
    QList<int> a = parseVersion(candidate);
    QList<int> b = parseVersion(current);
    if (a.isEmpty() || b.isEmpty())
        return false;
    const qsizetype n = std::max(a.size(), b.size());
    a.resize(n, 0); // 1.5 == 1.5.0
    b.resize(n, 0);
    return std::lexicographical_compare(b.begin(), b.end(), a.begin(), a.end());
}

PackageKind UpdateChecker::detectPackageKind() {
    if (QSysInfo::currentCpuArchitecture() != QLatin1String("x86_64"))
        return PackageKind::Unknown;
    if (!QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty())
        return PackageKind::Arch;
    if (!QStandardPaths::findExecutable(QStringLiteral("dpkg")).isEmpty())
        return PackageKind::Deb;
    return PackageKind::Unknown;
}

bool UpdateChecker::isTrustedAssetUrl(const QUrl& url) {
    return url.isValid() && url.scheme() == QLatin1String("https") &&
           url.host() == QLatin1String("github.com") && url.port() == -1 &&
           url.userInfo().isEmpty() &&
           url.path().startsWith(QStringLiteral("/%1/releases/download/").arg(QLatin1String(kRepo)));
}

UpdateChecker::ParseResult UpdateChecker::parseRelease(const QByteArray& json, PackageKind kind) {
    ParseResult out;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        out.error = QStringLiteral("Unexpected reply from the update server");
        return out;
    }
    const QJsonObject obj = doc.object();
    // Drafts and pre-releases are never offered ("latest" excludes them already; this is
    // for a mirror or a test server that does not).
    if (obj.value(QStringLiteral("draft")).toBool() ||
        obj.value(QStringLiteral("prerelease")).toBool()) {
        out.error = QStringLiteral("Latest release is a draft or pre-release");
        return out;
    }
    const QString tag = obj.value(QStringLiteral("tag_name")).toString();
    const QList<int> parts = parseVersion(tag);
    if (parts.isEmpty()) {
        out.error = QStringLiteral("Release has no usable version");
        return out;
    }
    QStringList strs;
    for (int p : parts)
        strs << QString::number(p);

    ReleaseInfo& r = out.release;
    r.version = strs.join(QLatin1Char('.'));
    r.notes = obj.value(QStringLiteral("body")).toString();
    const QUrl page(obj.value(QStringLiteral("html_url")).toString());
    // Only ever open a page on github.com ourselves.
    if (page.scheme() == QLatin1String("https") && page.host() == QLatin1String("github.com"))
        r.pageUrl = page;

    const QRegularExpression* pattern = kind == PackageKind::Arch   ? &archAssetPattern()
                                        : kind == PackageKind::Deb ? &debAssetPattern()
                                                                   : nullptr;
    if (pattern) {
        for (const QJsonValue& v : obj.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject a = v.toObject();
            const QString name = a.value(QStringLiteral("name")).toString();
            if (!pattern->match(name).hasMatch())
                continue;
            const QUrl url(a.value(QStringLiteral("browser_download_url")).toString());
            if (!isTrustedAssetUrl(url)) {
                qCWarning(lcUpdate) << "Ignoring asset with untrusted URL" << name;
                continue;
            }
            r.assetName = name;
            r.assetUrl = url;
            r.sha256 = normalizedSha256(a.value(QStringLiteral("digest")).toString());
            r.size = static_cast<qint64>(a.value(QStringLiteral("size")).toDouble());
            break;
        }
    }
    out.ok = true;
    return out;
}

QUrl UpdateChecker::changelogUrl(const QString& version) {
    const QByteArray env = qgetenv("VINDAUGA_UPDATE_CHANGELOG_URL");
    if (!env.isEmpty())
        return QUrl::fromEncoded(env);
    return QUrl(QStringLiteral("https://raw.githubusercontent.com/%1/v%2/CHANGELOG.md")
                    .arg(QLatin1String(kRepo), version));
}

QString UpdateChecker::changelogSince(const QString& changelog, const QString& current,
                                      const QString& latest) {
    struct Section {
        QString heading;
        QStringList lines;
    };
    QList<Section> sections;
    bool include = false;
    for (const QString& line : changelog.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("## "))) {
            const QString heading = line.mid(3).trimmed();
            const QString ver = heading.section(QLatin1Char(' '), 0, 0);
            include = !parseVersion(ver).isEmpty() &&
                      (ver == latest || (isNewer(ver, current) && !isNewer(ver, latest)));
            if (include)
                sections.append({heading, {}});
        } else if (include) {
            sections.last().lines << line;
        }
    }
    QStringList parts;
    for (const Section& s : sections) {
        const QString body = s.lines.join(QLatin1Char('\n')).trimmed();
        parts << (sections.size() > 1 ? QStringLiteral("### %1\n\n%2").arg(s.heading, body) : body);
    }
    return parts.join(QStringLiteral("\n\n"));
}

void UpdateChecker::fetchChangelog(const QString& latest, const QString& current) {
    if (m_notesReply)
        return;
    QNetworkRequest req(changelogUrl(latest));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Vindauga/%1").arg(QCoreApplication::applicationVersion()));
    req.setTransferTimeout(15000);
    req.setMaximumRedirectsAllowed(2);
    m_notesReply = m_nam->get(req);
    connect(m_notesReply, &QNetworkReply::finished, this, [this, latest, current]() {
        QNetworkReply* reply = m_notesReply;
        m_notesReply.clear();
        reply->deleteLater();
        QString text;
        if (reply->error() == QNetworkReply::NoError) {
            // A changelog is a few KB; cap what we are willing to parse and show.
            text = changelogSince(QString::fromUtf8(reply->read(256 * 1024)), current, latest);
        } else {
            qCInfo(lcUpdate) << "Changelog fetch failed:" << reply->errorString();
        }
        emit changelogFetched(latest, text);
    });
}

void UpdateChecker::check(PackageKind kind) {
    if (running())
        return;
    m_kind = kind;
    QNetworkRequest req(m_apiUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Vindauga/%1").arg(QCoreApplication::applicationVersion()));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setTransferTimeout(15000);
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::onReplyFinished);
}

void UpdateChecker::abort() {
    if (m_notesReply) {
        m_notesReply->disconnect(this);
        m_notesReply->abort();
        m_notesReply->deleteLater();
        m_notesReply.clear();
    }
    if (!m_reply)
        return;
    QNetworkReply* reply = m_reply;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    m_reply.clear();
}

void UpdateChecker::onReplyFinished() {
    QNetworkReply* reply = m_reply;
    m_reply.clear();
    reply->deleteLater();

    Result result;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status != 200) {
        result.error = status != 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
        // Rate limited: GitHub answers 403/429 and says when to come back.
        if (status == 403 || status == 429) {
            bool ok = false;
            qint64 wait = reply->rawHeader("Retry-After").toLongLong(&ok);
            if (!ok && reply->rawHeader("X-RateLimit-Remaining") == "0") {
                const qint64 reset = reply->rawHeader("X-RateLimit-Reset").toLongLong(&ok);
                if (ok)
                    wait = reset - QDateTime::currentSecsSinceEpoch();
            }
            if (ok && wait > 0)
                result.retryAfterSecs = std::min(wait, kMaxRetryAfterSecs);
        }
        qCInfo(lcUpdate) << "Update check failed:" << result.error;
        emit finished(result);
        return;
    }
    const ParseResult parsed = parseRelease(reply->readAll(), m_kind);
    result.ok = parsed.ok;
    result.error = parsed.error;
    result.release = parsed.release;
    if (!parsed.ok)
        qCWarning(lcUpdate) << "Update check: unusable reply:" << parsed.error;
    emit finished(result);
}

} // namespace vindauga
