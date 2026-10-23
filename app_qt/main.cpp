// app_qt/main.cpp -- the three-platform shell entry point.
//
// The window itself is MainWindow (app_qt/main_window.{h,cpp}); this file owns
// only the process contract:
//
//   msproject_qt                            open the window (interactive)
//   msproject_qt <file.mpp>                 open a plan
//   msproject_qt --open <file.mpp>          the same, explicit
//   msproject_qt --render <out.png>         render offscreen and exit 0
//   msproject_qt --open <file> --render <out.png>
//   msproject_qt --render <out.png> --parity
//   msproject_qt --render <out.png> --render-size 1920x1032
//
// --render is what CI runs with QT_QPA_PLATFORM=offscreen.  It paints the same
// QMainWindow the user gets (Fusion, fixed metrics) and fails if the pixmap is
// not the requested size, so the size is a checked promise rather than a side
// effect.  It is the seed of the cross-OS golden-image test in
// docs/ui-qt-port.md section 3.
//
// The window's DEFAULT size stays 900x560 (the pinned render tests); the render
// size is a property of the render request only:
//
//   --parity               shorthand for --render-size 1920x1032, the reference
//                          capture's own client size (docs/parity.md section 2).
//                          At that width all nine Entry columns fit, which is
//                          what makes P1's column-width comparison comparable.
//   --render-size WxH      any explicit size (200..8192 per side).
//
// Both are ignored when no --render is given: the interactive window keeps its
// sane default.

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QPixmap>
#include <QString>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "main_window.h"
#include "preferences_store.h"
#include "shell_style.h"

namespace {

struct Options {
    std::string open_path;
    std::string render_path;
    int render_width = msqt::kWindowWidth;
    int render_height = msqt::kWindowHeight;
};

void print_usage() {
    std::fprintf(stderr,
                 "usage: msproject_qt [--open <file.mpp>] [--render <out.png>]\n"
                 "                     [--parity | --render-size <WxH>]\n"
                 "       msproject_qt <file.mpp>\n");
}

// "1920x1032" (or "1920X1032") -> width, height.  False for anything else.
bool parse_size(const char* text, int& width, int& height) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    const long parsed_width = std::strtol(text, &end, 10);
    if (end == text || (*end != 'x' && *end != 'X')) {
        return false;
    }
    const char* height_text = end + 1;
    char* height_end = nullptr;
    const long parsed_height = std::strtol(height_text, &height_end, 10);
    if (height_end == height_text || *height_end != '\0') {
        return false;
    }
    if (parsed_width < 200 || parsed_width > 8192 || parsed_height < 200 ||
        parsed_height > 8192) {
        return false;
    }
    width = static_cast<int>(parsed_width);
    height = static_cast<int>(parsed_height);
    return true;
}

bool parse_options(int argc, char** argv, Options& out) {
    for (int index = 1; index < argc; ++index) {
        const char* argument = argv[index];
        if (std::strcmp(argument, "--open") == 0) {
            if (index + 1 >= argc) {
                return false;
            }
            out.open_path = argv[++index];
        } else if (std::strcmp(argument, "--render") == 0) {
            if (index + 1 >= argc) {
                return false;
            }
            out.render_path = argv[++index];
        } else if (std::strcmp(argument, "--parity") == 0) {
            out.render_width = msqt::kParityWidth;
            out.render_height = msqt::kParityHeight;
        } else if (std::strcmp(argument, "--render-size") == 0) {
            if (index + 1 >= argc ||
                !parse_size(argv[++index], out.render_width, out.render_height)) {
                return false;
            }
        } else if (argument[0] == '-') {
            return false;
        } else if (out.open_path.empty()) {
            out.open_path = argument;
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // One QSettings domain on every platform.  On macOS this is the
    // CFPreferences family NSUserDefaults uses; the stored payload and key are
    // app::serialize_preferences()/app::kPreferencesDefaultsKey (see
    // app_qt/preferences_store.h).
    QCoreApplication::setOrganizationName(QStringLiteral("com.msprojectdecompile"));
    QCoreApplication::setApplicationName(QStringLiteral("shell"));
    // Fusion style, one palette, one fixed body point size -- the same call the
    // offscreen test makes, so the two renders cannot drift.
    msqt::configure_application();

    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage();
        return 2;
    }

    msqt::MainWindow window;
    // Only the interactive window reads the user's preferences: --render is a
    // deterministic test mode, and a developer's saved date/zoom choice must not
    // reach a render or a parity artifact.
    std::unique_ptr<msqt::PreferencesStore> preferences_store;
    if (options.render_path.empty()) {
        preferences_store = std::make_unique<msqt::PreferencesStore>();
        app::Preferences initial = window.preferences();
        if (preferences_store->load(initial)) {
            window.apply_preferences(initial, false);
        }
        msqt::Theme initial_theme = window.theme();
        if (preferences_store->load_theme(initial_theme)) {
            window.set_theme(initial_theme, false);
        }
        window.set_preferences_store(preferences_store.get());
    }
    if (!options.open_path.empty()) {
        QString error;
        const QString path = QString::fromLocal8Bit(options.open_path.c_str());
        if (!window.open_file(path, &error)) {
            const QByteArray utf8 = error.toUtf8();
            std::fprintf(stderr, "error: could not open %s: %s\n", options.open_path.c_str(),
                         utf8.constData());
            if (!options.render_path.empty()) {
                return 1;
            }
        }
    }

    if (!options.render_path.empty()) {
        // Wave 21: the selection band is Project's measured green, which P1's rule
        // scan would read as a grid rule; a render is a structural snapshot, so it
        // is taken with nothing selected (the window itself keeps the selection).
        window.clear_render_selection();
        window.resize(options.render_width, options.render_height);
        window.show();
        QApplication::processEvents();
        // One more pass: the splitter's table-edge snap is applied from a queued
        // call after the first layout has sized the window.
        QApplication::processEvents();
        const QPixmap pixmap = window.grab();
        if (pixmap.width() != options.render_width ||
            pixmap.height() != options.render_height) {
            std::fprintf(stderr, "error: rendered %dx%d, expected %dx%d\n", pixmap.width(),
                         pixmap.height(), options.render_width, options.render_height);
            return 1;
        }
        if (!pixmap.save(QString::fromLocal8Bit(options.render_path.c_str()), "PNG")) {
            std::fprintf(stderr, "error: could not write %s\n", options.render_path.c_str());
            return 1;
        }
        std::printf("rendered %dx%d to %s\n", pixmap.width(), pixmap.height(),
                    options.render_path.c_str());
        return 0;
    }

    window.show();
    return app.exec();
}
