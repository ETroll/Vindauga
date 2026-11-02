#include "UpdateInstaller.h"

#include <QProcess>
#include <QStandardPaths>

#include "Logging.h"

namespace vindauga {

namespace {

// $1 = package file, $2 = file name to use for the root-owned copy, $3 = sha256.
// 755/644 so the package manager's sandbox user (pacman's alpm, apt's _apt) can read the
// copy; the directory itself is root-owned, so nobody else can replace the file.
const char* kScript =
    "set -e\n"
    "d=$(mktemp -d)\n"
    "trap 'rm -rf \"$d\"' EXIT\n"
    "cp -- \"$1\" \"$d/$2\"\n"
    "chmod 755 \"$d\"\n"
    "chmod 644 \"$d/$2\"\n"
    "echo \"$3  $d/$2\" | sha256sum -c --status -\n";

QString rootCopyName(PackageKind kind) {
    return kind == PackageKind::Arch ? QStringLiteral("vindauga.pkg.tar.zst")
                                     : QStringLiteral("vindauga.deb");
}

QString tail(QString text, int maxLines = 12) {
    const QStringList lines = text.trimmed().split(QLatin1Char('\n'));
    return lines.mid(std::max<qsizetype>(0, lines.size() - maxLines)).join(QLatin1Char('\n'));
}

} // namespace

UpdateInstaller::UpdateInstaller(QObject* parent) : QObject(parent) {}

UpdateInstaller::~UpdateInstaller() = default; // QProcess is a child; not killed on purpose

QStringList UpdateInstaller::pkexecArguments(PackageKind kind, const QString& file,
                                             const QString& sha256) {
    QString script = QString::fromLatin1(kScript);
    if (kind == PackageKind::Arch)
        script += QStringLiteral("pacman -U --noconfirm \"$d/$2\"\n");
    else
        script += QStringLiteral("DEBIAN_FRONTEND=noninteractive apt-get install -y \"$d/$2\"\n");
    return {QStringLiteral("/bin/sh"), QStringLiteral("-c"), script, QStringLiteral("sh"),
            file, rootCopyName(kind), sha256};
}

QString UpdateInstaller::manualCommand(PackageKind kind, const QString& file) {
    // Single-quote the path (it can contain a space via $XDG_CACHE_HOME).
    QString quoted = file;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    quoted = QLatin1Char('\'') + quoted + QLatin1Char('\'');
    return kind == PackageKind::Arch ? QStringLiteral("sudo pacman -U %1").arg(quoted)
                                     : QStringLiteral("sudo apt install %1").arg(quoted);
}

void UpdateInstaller::start(PackageKind kind, const QString& file, const QString& sha256) {
    if (running())
        return;
    if (kind == PackageKind::Unknown) {
        emit finished(Outcome::Failed, tr("No supported package manager found"));
        return;
    }
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty()) {
        emit finished(Outcome::NoAgent, tr("pkexec (polkit) is not installed"));
        return;
    }
    auto* process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            process->deleteLater();
            emit finished(Outcome::NoAgent, tr("Could not start pkexec"));
        }
    });
    connect(process, &QProcess::finished, this, &UpdateInstaller::onFinished);
    process->setProcessChannelMode(QProcess::MergedChannels);
    qCInfo(lcUpdate) << "Installing update via pkexec," << (kind == PackageKind::Arch ? "pacman" : "apt-get");
    process->start(pkexec, pkexecArguments(kind, file, sha256));
}

void UpdateInstaller::onFinished(int exitCode, int exitStatus) {
    QProcess* process = m_process;
    const QString output = tail(QString::fromLocal8Bit(process->readAll()));
    process->deleteLater();
    m_process.clear();
    qCInfo(lcUpdate) << "Update installer exited with" << exitCode << "status" << exitStatus;
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        emit finished(Outcome::Ok, QString());
    } else if (exitStatus == QProcess::NormalExit && exitCode == 126) {
        emit finished(Outcome::Dismissed, QString());
    } else if (exitStatus == QProcess::NormalExit && exitCode == 127) {
        emit finished(Outcome::NoAgent, output);
    } else {
        emit finished(Outcome::Failed, output);
    }
}

} // namespace vindauga
