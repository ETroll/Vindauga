#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "UpdateChecker.h"

class QProcess;

// Installs a downloaded, already verified package as root through polkit (pkexec), which
// gives a graphical password prompt on any desktop that runs a polkit agent.
//
// The privileged step does not trust the file in the user's cache directory: it first
// copies it to a root-owned temporary directory and verifies the SHA-256 there, then
// installs the copy. A process running as the user can therefore not swap the package
// between our check and root reading it.
namespace vindauga {

class UpdateInstaller : public QObject {
    Q_OBJECT
public:
    enum class Outcome {
        Ok,
        Dismissed, // the user closed the password dialog or typed a wrong password
        NoAgent,   // pkexec could not ask (no polkit agent running / not authorised)
        Failed,    // pkexec ran but the package manager (or the checksum) failed
    };
    Q_ENUM(Outcome)

    explicit UpdateInstaller(QObject* parent = nullptr);
    ~UpdateInstaller() override;

    // Arguments for pkexec (program is always /bin/sh -c ...). Exposed for tests.
    static QStringList pkexecArguments(PackageKind kind, const QString& file,
                                       const QString& sha256);
    // What the user can paste into a terminal when pkexec is not an option.
    static QString manualCommand(PackageKind kind, const QString& file);

    void start(PackageKind kind, const QString& file, const QString& sha256);
    bool running() const { return !m_process.isNull(); }

signals:
    // details: the tail of the package manager's output for Failed/NoAgent, else empty.
    void finished(vindauga::UpdateInstaller::Outcome outcome, const QString& details);

private:
    void onFinished(int exitCode, int exitStatus);

    QPointer<QProcess> m_process;
};

} // namespace vindauga
