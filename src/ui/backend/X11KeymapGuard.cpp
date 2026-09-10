#include "X11KeymapGuard.h"

#include "Logging.h"

#include <QList>
#include <QtGlobal>

#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace vindauga {

namespace {

// The path Chromium's XkbKeyboardLayoutEngine hard-codes (XKB_CONTEXT_NO_DEFAULT_INCLUDES
// + xkb_context_include_path_append("/usr/share/X11/xkb")). It is the only include path
// in libQt6WebEngineCore; XKB_CONFIG_EXTRA_PATH and ~/.config/xkb are ignored there.
constexpr const char kChromiumXkbPath[] = "/usr/share/X11/xkb";
constexpr const char kPropertyName[] = "_XKB_RULES_NAMES";
constexpr uint32_t kPropertyMaxLongs = 1024;

void xkbLogToQt(xkb_context*, xkb_log_level, const char* format, va_list args) {
    char buf[512];
    std::vsnprintf(buf, sizeof buf, format, args);
    qCDebug(lcX11) << "xkbcommon:" << QByteArray(buf).trimmed();
}

QList<QByteArray> splitGroups(const QByteArray& list) {
    return list.split(',');
}

} // namespace

std::optional<XkbRulesNames> X11KeymapGuard::parseProperty(const QByteArray& raw) {
    if (raw.isEmpty())
        return std::nullopt;
    // The property is "rules\0model\0layout\0variant\0options\0" (each field NUL-terminated).
    QList<QByteArray> fields = raw.split('\0');
    if (!fields.isEmpty() && fields.last().isEmpty() && raw.endsWith('\0'))
        fields.removeLast();
    while (fields.size() < 5)
        fields.append(QByteArray());
    XkbRulesNames names;
    names.rules = fields[0];
    names.model = fields[1];
    names.layout = fields[2];
    names.variant = fields[3];
    names.options = fields[4];
    if (names.layout.isEmpty())
        return std::nullopt; // nothing Chromium can fail on; libxkbcommon defaults to "us"
    return names;
}

QByteArray X11KeymapGuard::serializeProperty(const XkbRulesNames& names) {
    QByteArray out;
    for (const QByteArray* field : {&names.rules, &names.model, &names.layout, &names.variant,
                                    &names.options}) {
        out += *field;
        out += '\0';
    }
    return out;
}

bool X11KeymapGuard::groupCompiles(const QByteArray& layout, const QByteArray& variant) {
    std::unique_ptr<xkb_context, decltype(&xkb_context_unref)> ctx(
        xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES), &xkb_context_unref);
    if (!ctx)
        return true; // cannot evaluate: behave as if everything is fine and touch nothing
    xkb_context_set_log_fn(ctx.get(), xkbLogToQt);
    if (!xkb_context_include_path_append(ctx.get(), kChromiumXkbPath))
        return false; // Chromium would not find anything either

    xkb_rule_names names{};
    names.rules = nullptr; // XKB_DEFAULT_RULES / "evdev", as in Chromium
    names.model = "pc101"; // Chromium's hard-coded model
    names.layout = layout.constData();
    names.variant = variant.constData();
    names.options = "";
    std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)> keymap(
        xkb_keymap_new_from_names(ctx.get(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS),
        &xkb_keymap_unref);
    return keymap != nullptr;
}

bool X11KeymapGuard::compiles(const XkbRulesNames& names) {
    const QList<QByteArray> layouts = splitGroups(names.layout);
    QList<QByteArray> variants = splitGroups(names.variant);
    while (variants.size() < layouts.size())
        variants.append(QByteArray());
    for (qsizetype i = 0; i < layouts.size(); ++i) {
        if (!groupCompiles(layouts[i], variants[i]))
            return false;
    }
    // Also the whole list at once: the per-group check mirrors Chromium's one group at a
    // time, but a list that does not compile as a whole must not be published as
    // "repaired" either, since every other name-based reader sees the same list.
    return groupCompiles(names.layout, names.variant);
}

std::optional<XkbRulesNames> X11KeymapGuard::repair(const XkbRulesNames& names) {
    QList<QByteArray> layouts = splitGroups(names.layout);
    QList<QByteArray> variants = splitGroups(names.variant);
    while (variants.size() < layouts.size())
        variants.append(QByteArray());
    // Surplus variants without a matching layout are meaningless; cut them.
    variants = variants.mid(0, layouts.size());

    bool changed = false;
    for (qsizetype i = 0; i < layouts.size(); ++i) {
        if (groupCompiles(layouts[i], variants[i]))
            continue;
        if (!variants[i].isEmpty() && groupCompiles(layouts[i], QByteArray())) {
            variants[i].clear();
            changed = true;
            continue;
        }
        if (groupCompiles(QByteArrayLiteral("us"), QByteArray())) {
            layouts[i] = QByteArrayLiteral("us");
            variants[i].clear();
            changed = true;
            continue;
        }
        return std::nullopt;
    }

    XkbRulesNames out = names;
    if (changed) {
        out.layout = layouts.join(',');
        bool anyVariant = false;
        for (const QByteArray& v : variants)
            anyVariant = anyVariant || !v.isEmpty();
        out.variant = anyVariant ? variants.join(',') : QByteArray();
    }
    if (compiles(out))
        return out;
    // Groups are fine individually but not together (e.g. empty groups "us," + ",de"):
    // last resort is the whole list without variants; otherwise give up and touch nothing.
    out.variant.clear();
    if (compiles(out))
        return out;
    return std::nullopt;
}

bool X11KeymapGuard::apply() {
    // Own xcb connection from $DISPLAY, independent of Qt's QPA platform, because that
    // is exactly what Chromium does: QtWebEngine opens the X display and reads the
    // property itself even under QT_QPA_PLATFORM=offscreen, and likewise in a Wayland
    // session with XWayland (DISPLAY set).
    if (qEnvironmentVariableIsEmpty("DISPLAY")) {
        qCDebug(lcX11) << "No DISPLAY — Chromium has no X keymap to fail on";
        return false;
    }
    int screenNumber = 0;
    std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection(
        xcb_connect(nullptr, &screenNumber), &xcb_disconnect);
    xcb_connection_t* conn = connection.get();
    if (!conn || xcb_connection_has_error(conn)) {
        qCDebug(lcX11) << "Could not connect to the X server on DISPLAY="
                       << qEnvironmentVariable("DISPLAY");
        return false;
    }
    // Root window of DISPLAY's default screen (Xlib's DefaultRootWindow, where setxkbmap
    // writes and Chromium reads).
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(conn));
    for (int i = 0; i < screenNumber && screens.rem > 0; ++i)
        xcb_screen_next(&screens);
    if (screens.rem <= 0 || !screens.data)
        return false;
    const xcb_window_t root = screens.data->root;

    const xcb_intern_atom_cookie_t atomCookie =
        xcb_intern_atom(conn, 1 /* only_if_exists */, std::strlen(kPropertyName), kPropertyName);
    std::unique_ptr<xcb_intern_atom_reply_t, decltype(&std::free)> atomReply(
        xcb_intern_atom_reply(conn, atomCookie, nullptr), &std::free);
    if (!atomReply || atomReply->atom == XCB_ATOM_NONE) {
        qCDebug(lcX11) << kPropertyName << "does not exist on the X server — nothing to check";
        return false;
    }
    const xcb_atom_t atom = atomReply->atom;

    const xcb_get_property_cookie_t propCookie =
        xcb_get_property(conn, 0, root, atom, XCB_ATOM_STRING, 0, kPropertyMaxLongs);
    std::unique_ptr<xcb_get_property_reply_t, decltype(&std::free)> propReply(
        xcb_get_property_reply(conn, propCookie, nullptr), &std::free);
    if (!propReply || propReply->type != XCB_ATOM_STRING || propReply->format != 8) {
        qCDebug(lcX11) << kPropertyName << "missing/unexpected type on the root window";
        return false;
    }
    if (propReply->bytes_after != 0) {
        // Unreasonably long (> 4 KiB); better to leave it than write back a truncated value.
        qCWarning(lcX11) << kPropertyName << "is longer than expected (" << propReply->bytes_after
                         << "bytes remaining) — leaving it untouched";
        return false;
    }
    const QByteArray raw(static_cast<const char*>(xcb_get_property_value(propReply.get())),
                         xcb_get_property_value_length(propReply.get()));
    const std::optional<XkbRulesNames> names = parseProperty(raw);
    if (!names) {
        qCDebug(lcX11) << kPropertyName << "empty/unparsable — nothing to check";
        return false;
    }
    if (compiles(*names)) {
        qCDebug(lcX11) << "XKB configuration OK for Chromium:" << names->layout << names->variant;
        return false;
    }

    const std::optional<XkbRulesNames> fixed = repair(*names);
    if (!fixed) {
        qCWarning(lcX11) << "XKB configuration" << names->layout << names->variant
                         << "does not compile (Chromium/QtWebEngine will crash at sign-in),"
                         << "and cannot be repaired — is xkeyboard-config installed in"
                         << kChromiumXkbPath << "?";
        return false;
    }

    const QByteArray out = serializeProperty(*fixed);
    // _checked + xcb_request_check gives both the error code and a synchronous round trip
    // guaranteeing the X server has performed the write before we return. Chromium reads
    // the property later over its own connection, so a plain xcb_flush() would only
    // guarantee the request was sent, not executed.
    const xcb_void_cookie_t changeCookie =
        xcb_change_property_checked(conn, XCB_PROP_MODE_REPLACE, root, atom, XCB_ATOM_STRING, 8,
                                    static_cast<uint32_t>(out.size()), out.constData());
    std::unique_ptr<xcb_generic_error_t, decltype(&std::free)> changeError(
        xcb_request_check(conn, changeCookie), &std::free);
    if (changeError || xcb_connection_has_error(conn)) {
        qCWarning(lcX11) << "Could not rewrite" << kPropertyName << "on root window"
                         << Qt::hex << root << "— X error code"
                         << (changeError ? int(changeError->error_code) : -1)
                         << "connection error" << xcb_connection_has_error(conn)
                         << "; QtWebEngine will probably crash at sign-in.";
        return false;
    }
    qCWarning(lcX11) << "XKB configuration in" << kPropertyName << "on root window" << Qt::hex << root
                     << Qt::dec << "(" << names->layout
                     << names->variant << ") does not exist in the installed xkeyboard-config —"
                     << "QtWebEngine would have crashed at sign-in. Rewrote the property to"
                     << "(" << fixed->layout << fixed->variant << "). The X server's actual"
                     << "keymap is untouched; permanent fix: set a valid variant, e.g."
                     << "\"winkeys\" instead of \"win\" (setxkbmap / /etc/default/keyboard).";
    return true;
}

} // namespace vindauga
