// src/ui/shortcuts/HyprlandBindsSync.h
//
// Hyprland-specific convenience: write the `bind = ..., global, :<id>` lines
// that route physical keys to our xdg-desktop-portal shortcuts directly
// into the user's hyprland.conf, then run `hyprctl reload`.
//
// Why: xdg-desktop-portal-hyprland accepts BindShortcuts and remembers the
// shortcut ids, but the compositor only forwards a physical key to the
// portal once a matching `bind = ..., global, <app_id>:<shortcut_id>`
// entry is present in hyprland.conf. Without that line, shortcut changes
// only take effect after the user manually edits the config and reloads.
// This helper closes the loop: on every portal session commit, the managed
// block is rewritten atomically and hyprctl reload makes it live.
//
// The block is delimited by marker comments so we never touch unrelated
// lines:
//
//     # >>> simpleautoframer: managed block — do not edit by hand <<<
//     bind = CTRL ALT, 1, global, :ctrl_alt_1
//     # <<< simpleautoframer: end managed block >>>
//
// No-ops (returns false without touching anything) when the environment
// doesn't look like Hyprland, or when no hyprland.conf can be located.

#pragma once

#include "IShortcutManager.h"

#include <vector>

namespace saf {

bool syncHyprlandBinds(const std::vector<ShortcutDescriptor> &shortcuts);

} // namespace saf
