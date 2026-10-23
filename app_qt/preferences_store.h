// app_qt/preferences_store.h -- the Qt shell's preference store.
//
// app/main.mm keeps its preferences in NSUserDefaults under the one key
// app::kPreferencesDefaultsKey, holding ONE line produced by
// app::serialize_preferences() and read back by app::parse_preferences()
// (app/prefs.h explains why the preferences are app-side and never written into
// a .mpp).  The Qt shell must persist the same payload through the same model
// path on all three platforms, so this class is the portable equivalent:
//
//   * the string is exactly app::serialize_preferences(prefs);
//   * the key is exactly app::kPreferencesDefaultsKey;
//   * the backend is Qt's native QSettings store (the registry on Windows, a
//     plist on macOS, an ini/config file on Linux).  On macOS that is the same
//     CFPreferences family NSUserDefaults uses, though in this app's own
//     organisation/application domain rather than the bundle identifier -- the
//     serialized payload and its key are identical, the container differs
//     because the Qt shell is not the AppKit bundle.
//
// The constructor that takes a path is the test seam: it opens an explicit ini
// file so a gate can prove the round trip without touching the user's real
// preferences.  The offscreen tests pass a file under the build tree.
#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

#include <memory>

#include "prefs.h"
#include "theme.h"

namespace msqt {

class PreferencesStore {
  public:
    // Native QSettings store.  QCoreApplication::organizationName() and
    // applicationName() must be set before this is constructed (main.cpp does).
    PreferencesStore();
    // An explicit ini file; used by tests (never the user's real store).
    explicit PreferencesStore(const QString& file_path);

    // Reads the stored line.  Returns false (leaving `out` untouched) when the
    // key is absent or the line carries no known key, exactly like the macOS
    // shell's `if (stored != nil) parse_preferences(...)`.
    bool load(app::Preferences& out) const;
    // Writes app::serialize_preferences(preferences) under the shared key.
    void save(const app::Preferences& preferences);

    // The Office theme is separate from app::Preferences (which is the shared
    // model); it lives under its own key beside the preference line.
    bool load_theme(Theme& out) const;
    void save_theme(Theme theme);

    // W26-4.3: the local recent-file list the New/Open/Save As pages show.  It
    // is a plain list of local paths (no cloud entries, no URLs) under its own
    // key; add_recent_file() moves an existing path to the front and keeps at
    // most kRecentFileLimit entries.
    static constexpr int kRecentFileLimit = 10;
    QStringList load_recent_files() const;
    void add_recent_file(const QString& path);

    // True when the backend could be opened for writing (a read-only store
    // reports false so the shell can say persistence failed).
    bool writable() const;
    QString file_path() const;

  private:
    std::unique_ptr<QSettings> settings_;
};

}  // namespace msqt
