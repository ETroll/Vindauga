#pragma once
#include <QList>
#include <QSet>
#include <Qt>
#include <QtGlobal>
#include <utility>

namespace vindauga {

// Tracks which keys and mouse buttons the local user is holding, so that they can be
// released on the remote side when local key-up events stop arriving. That happens when
// the window loses focus while a key is down (lock screen, compositor shortcut, another
// window taking focus): the key-up goes elsewhere, and without a release the remote
// session keeps the key down. Not thread-safe; owned by the GUI thread.
class PressedInputTracker {
public:
    struct Releases {
        QList<quint32> keys;
        QList<Qt::MouseButton> buttons;
        bool isEmpty() const { return keys.isEmpty() && buttons.isEmpty(); }
    };

    void keyDown(quint32 nativeScanCode) { m_keys.insert(nativeScanCode); }
    void keyUp(quint32 nativeScanCode) { m_keys.remove(nativeScanCode); }

    void buttonDown(Qt::MouseButton button) { m_buttons.insert(static_cast<int>(button)); }
    void buttonUp(Qt::MouseButton button) { m_buttons.remove(static_cast<int>(button)); }

    // Forgets all held input and returns what has to be released.
    Releases takeAll() {
        Releases releases;
        releases.keys = m_keys.values();
        for (int button : std::as_const(m_buttons))
            releases.buttons.append(static_cast<Qt::MouseButton>(button));
        clear();
        return releases;
    }

    void clear() {
        m_keys.clear();
        m_buttons.clear();
    }

private:
    QSet<quint32> m_keys;
    QSet<int> m_buttons;
};

} // namespace vindauga
