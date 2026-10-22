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
//
// --render is what CI runs with QT_QPA_PLATFORM=offscreen.  It paints the same
// QMainWindow the user gets (Fusion, fixed metrics, 900x560) and fails if the
// pixmap is not that fixed size, so the size is a checked promise rather than a
// side effect.  It is the seed of the cross-OS golden-image test in
// docs/ui-qt-port.md section 3.

#include <QApplication>
#include <QByteArray>
#include <QPixmap>
#include <QString>

#include <cstdio>
#include <cstring>
#include <string>

#include "main_window.h"
#include "shell_style.h"

namespace {

struct Options {
    std::string open_path;
    std::string render_path;
};

void print_usage() {
    std::fprintf(stderr,
                 "usage: msproject_qt [--open <file.mpp>] [--render <out.png>]\n"
                 "       msproject_qt <file.mpp>\n");
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
    // Fusion style, one palette, one fixed body point size -- the same call the
    // offscreen test makes, so the two renders cannot drift.
    msqt::configure_application();

    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage();
        return 2;
    }

    msqt::MainWindow window;
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
        window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
        window.show();
        QApplication::processEvents();
        const QPixmap pixmap = window.grab();
        if (pixmap.width() != msqt::kWindowWidth || pixmap.height() != msqt::kWindowHeight) {
            std::fprintf(stderr, "error: rendered %dx%d, expected %dx%d\n", pixmap.width(),
                         pixmap.height(), msqt::kWindowWidth, msqt::kWindowHeight);
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
