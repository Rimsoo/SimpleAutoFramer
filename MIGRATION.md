# Migration guide — applying the fixes

This is the order I recommend applying the deliverables. Each step is
independent enough to commit and test on its own.

---

## Step 1 — Hygiene (5 minutes)

1. Add `LICENSE` at the repo root. Pick one:
   - **MIT** — most permissive, maximally compatible.
   - **GPL-3.0-or-later** — copyleft; consistent with v4l2loopback and
     NVIDIA Broadcast "alternative" framing.
2. Replace `.gitignore` with the provided one.
3. Remove `nohup.out` from the repo:
   ```bash
   git rm --cached nohup.out && git commit -m "chore: stop tracking nohup.out"
   ```

## Step 2 — CMake (10 minutes)

Replace `CMakeLists.txt` with the provided version.
Key changes you'll notice:
- `project(... LANGUAGES CXX)` — CUDA is now opt-in via `-DUSE_CUDA=ON`.
- `USE_X11_SHORTCUTS` and `USE_PORTAL_SHORTCUTS` options.
- `pkg_check_modules(... IMPORTED_TARGET ...)` — cleaner link lines.
- Explicit source list (recommended). If you want to keep `file(GLOB)`
  for now, use the commented-out block with `CONFIGURE_DEPENDS`.
- `GNUInstallDirs` + relative install paths so `DESTDIR` works for
  packaging.

Add this at the top of your `project()` call if you want versioning:
```cmake
project(simpleautoframer VERSION 0.2.0 LANGUAGES CXX)
```

## Step 3 — Shortcut abstraction (1 hour + your refactor)

1. Drop the five files into `src/ui/shortcuts/`:
   - `IShortcutManager.h`
   - `ShortcutManagerFactory.cpp`
   - `X11ShortcutManager.{h,cpp}`
   - `PortalShortcutManager.{h,cpp}`
2. **Port your existing X11 code.** The skeleton's `eventLoop()` covers
   the main pattern (XGrabKey + XNextEvent). If your previous code used
   `XISelectEvents` with XInput2 per-device, replace the body of
   `eventLoop()` accordingly — the rest of the class stays the same.
3. Wire the factory from `main.cpp`. See
   `main_shortcut_integration_example.cpp` for the pattern. The critical
   rule: **`registerShortcut()` before `start()`** for the portal.
4. Remove your old direct `XGrabKey`/`XISelectEvents` calls from the rest
   of the codebase. Everything must now go through `IShortcutManager`.

## Step 4 — install.sh + build_and_run.sh (5 minutes)

Replace both. Then test:
```bash
./install.sh --no-install    # build only, no sudo
./install.sh                 # full install
./install.sh --no-deps       # skip package-manager
```

## Step 5 — AUR packaging (30 minutes)

1. Copy `packaging/arch/` to a separate local working dir (AUR expects
   one repo per package):
   ```bash
   cp -r packaging/arch /tmp/simpleautoframer-git
   cd /tmp/simpleautoframer-git
   ```
2. Edit `PKGBUILD` — set `license=(...)` to match your LICENSE.
3. Generate real checksums:
   ```bash
   updpkgsums
   ```
4. Test-build locally:
   ```bash
   makepkg -si
   ```
5. Generate `.SRCINFO`:
   ```bash
   makepkg --printsrcinfo > .SRCINFO
   ```
6. Push to AUR:
   ```bash
   git clone ssh://aur@aur.archlinux.org/simpleautoframer-git.git aur
   cp PKGBUILD .SRCINFO simpleautoframer.install v4l2loopback*.conf aur/
   cd aur && git add . && git commit -m "Initial import" && git push
   ```

## Step 6 — Runtime audit of your sources (variable effort)

I could not inspect your `src/*.cpp` files directly. Please grep your code
for the following patterns and confirm each is handled:

### Crash-guards

- [ ] `cv::VideoCapture::open()` wrapped in try/catch + `isOpened()` check.
- [ ] `cv::dnn::readNetFromCaffe()` wrapped in try/catch.
- [ ] `cv::CascadeClassifier::load()` return value checked.
- [ ] `Gtk::Builder::create_from_file()` / `Gtk::Builder::add_from_file()`
      wrapped in try/catch — `Glib::FileError`, `Glib::MarkupError`,
      `Gtk::BuilderError`.
- [ ] All resource paths built via `saf::resourcePath("...")` (from the
      new `config.h`), never hard-coded to `/usr/share/...`.

### Concurrency

- [ ] Any shared state between capture thread, processing thread, UI
      thread and shortcut callback guarded by a mutex.
- [ ] Config values (zoom, smoothing, enabled/disabled) atomic
      (`std::atomic<float>` via a small helper) or behind a mutex.

### Lifecycle

- [ ] SIGINT/SIGTERM handler that sets a `std::atomic<bool>` stop flag.
- [ ] In main(), on exit: stop shortcuts → release camera
      (`VideoCapture::release()`) → close v4l2loopback sink → join
      worker threads.
- [ ] Do NOT call Gtk-related functions from the signal handler — only
      set flags.

### v4l2 sink

- [ ] Before opening `/dev/video20`, check it exists:
      `std::filesystem::exists("/dev/video20")`.
      If not, log a helpful message: "run `sudo modprobe v4l2loopback`".
- [ ] Format negotiation: confirm pix_fmt + resolution match before
      writing frames.

### Config file

- [ ] Config path `~/.config/simpleautoframer/config.json` (XDG).
- [ ] JSON parse errors caught and handled (fall back to defaults).
- [ ] File written atomically: write to `.tmp`, then `rename()`.

## Step 7 — Windows port (long-term)

Only start this once steps 1–6 are in and stable.
Order inside the port:

1. Abstract `ICameraSource` and make Linux use it (existing code moves
   into `V4L2Source`).
2. Abstract `IVirtualCameraSink` likewise.
3. Drop in `Win32ShortcutManager.cpp` (provided).
4. Migrate UI: either keep GTK (fragile on Windows) or port to Qt6.
   Recommendation: **Qt6**. The config/framer core is UI-agnostic, so
   this is a finite task.
5. Implement `MediaFoundationSource` (`cv::CAP_MSMF` is a quick start).
6. Implement virtual-camera sink:
   - MVP: bundle [UnityCaptureFilter] as a redistributable DirectShow
     filter + memory-mapped file transport.
   - Long term: native `IMFVirtualCamera` (Windows 11 22H2+), MSIX pkg.
