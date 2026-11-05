// src/ui/shortcuts/HyprlandBindsSync.h
//
// Hyprland-specific convenience: make the compositor route our global
// shortcuts to the xdg-desktop-portal session, then reload Hyprland.
//
// Why: xdg-desktop-portal-hyprland accepts BindShortcuts, but the compositor
// only forwards a physical key once a matching `global` bind exists in its
// config. The bind argument is "<app_id>:<shortcut id>"; the app registers
// itself with the portal as SAF_APP_ID so that prefix is stable.
//
// Two config flavours are supported:
//   * hyprland.lua (Hyprland >= 0.55): the binds go into a generated
//     simpleautoframer.lua next to it, loaded by a one-line managed block:
//         hl.bind("CTRL + ALT + 1", hl.dsp.global("org.simple_auto_framer:ctrl_alt_1"))
//   * hyprland.conf (legacy hyprlang): a managed block holding
//         bind = CTRL ALT, 1, global, org.simple_auto_framer:ctrl_alt_1
//
// Managed blocks are delimited by marker comments so unrelated lines are
// never touched. Symlinked configs (dotfile managers) are written through.
// The feature is controlled by the `hyprland_sync` setting.

#pragma once

#include "IShortcutManager.h"

#include <vector>

namespace saf {

/// True when running inside a Hyprland session.
bool isHyprlandSession();

/// Write the binds for `shortcuts` and reload Hyprland. No-op (returns
/// false) outside Hyprland or when no config file can be found.
bool syncHyprlandBinds(const std::vector<ShortcutDescriptor> &shortcuts);

/// Remove everything syncHyprlandBinds() added, then reload Hyprland.
bool removeHyprlandBinds();

} // namespace saf
