#pragma once

#include <QByteArray>

#include <optional>

namespace vindauga {

// Contents of the X server's `_XKB_RULES_NAMES` property on the root window (what
// `setxkbmap -query` shows). `layout`/`variant` are comma-separated lists, parallel per
// XKB group ("no,us" / "win,").
struct XkbRulesNames {
    QByteArray rules;
    QByteArray model;
    QByteArray layout;
    QByteArray variant;
    QByteArray options;
    bool operator==(const XkbRulesNames&) const = default;
};

// Pre-flight check of the keyboard layout QtWebEngine (Chromium) compiles itself at
// startup.
//
// Chromium's XkbKeyboardLayoutEngine reads layout+variant as names from
// `_XKB_RULES_NAMES` and compiles them with libxkbcommon against a hard-coded
// /usr/share/X11/xkb, and terminates the whole process with LOG(FATAL) ("Keymap file
// failed to load: no-win", SIGTRAP) if compilation fails. That happens when the property
// advertises a variant that does not exist in the installed xkeyboard-config (e.g.
// `no(win)`, which is actually called `winkeys`). The X server itself silently falls
// back to the plain layout and works, so nothing is noticed until the first AAD sign-in
// crashes the application.
//
// The pure helpers (parse/serialize/compiles/repair) have no X dependency and are unit
// tested; only apply() talks to the X server.
class X11KeymapGuard {
public:
    // Raw property value (NUL-separated fields) to struct. nullopt if empty or unparsable.
    static std::optional<XkbRulesNames> parseProperty(const QByteArray& raw);
    // Struct to raw property value, in the format setxkbmap/XkbRF_SetNamesProp write.
    static QByteArray serializeProperty(const XkbRulesNames& names);

    // Compiles one group (layout + variant) the way Chromium does: default rules,
    // Chromium's hard-coded model, no options, /usr/share/X11/xkb as the only include path.
    static bool groupCompiles(const QByteArray& layout, const QByteArray& variant);
    // True if every group in names compiles on its own (as Chromium does it) and the
    // whole list compiles together.
    static bool compiles(const XkbRulesNames& names);

    // Minimal repair per non-compiling group: (1) drop the variant (what the X server
    // does for an unknown variant), (2) fall back to "us" (what the X server does when the
    // whole keymap fails: it loads its built-in default). Returns a compiling copy
    // (unchanged if names already compiles), or nullopt if even that does not help (e.g.
    // xkeyboard-config missing entirely), in which case nothing is touched.
    static std::optional<XkbRulesNames> repair(const XkbRulesNames& names);

    // Run once at startup, before the first QtWebEngine object is created. Opens its own
    // xcb connection from $DISPLAY (exactly like Chromium, independent of Qt's QPA, so it
    // also applies offscreen and under Wayland+XWayland). No-op (false) without a
    // DISPLAY/X server, when the property is missing, when it already compiles, or when
    // it cannot be repaired. Otherwise writes the repaired property back to the root
    // window (the same property setxkbmap sets; the X server's actually loaded keymap is
    // not touched) and returns true.
    static bool apply();
};

} // namespace vindauga
