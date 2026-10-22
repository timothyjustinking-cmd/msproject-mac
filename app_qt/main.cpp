// app_qt/main.cpp -- the three-platform shell, step 0.
//
// This is deliberately small: wave 16 starts by proving the toolchain (Qt 6
// Widgets builds on the Linux runner, renders offscreen, and can be driven by a
// test) before any of the app's models are wired in.  The window is built from a
// single widget so the golden-image harness has something deterministic to
// compare across OSes.
//
//   msproject_qt                      open the window (interactive)
//   msproject_qt --render <out.png>   render offscreen and exit 0
//
// The --render mode is what CI runs with QT_QPA_PLATFORM=offscreen; it is the
// seed of the cross-OS golden-image test in docs/ui-qt-port.md.

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>
#include <cstring>

namespace {

// One place for the fidelity contract's fixed metrics: the golden images depend
// on these, so they are constants, not defaults inherited from the platform.
constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 560;
constexpr int kHeaderPointSize = 13;

QMainWindow* build_window() {
    auto* window = new QMainWindow;
    window->setWindowTitle(QStringLiteral("msproject"));
    window->resize(kWindowWidth, kWindowHeight);

    auto* central = new QWidget(window);
    auto* layout = new QVBoxLayout(central);
    auto* header = new QLabel(QStringLiteral("msproject"), central);
    QFont font = header->font();
    font.setPointSize(kHeaderPointSize);
    header->setFont(font);
    layout->addWidget(header);
    layout->addStretch(1);
    central->setLayout(layout);
    window->setCentralWidget(central);
    return window;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Fusion everywhere: the same widget painting on Windows, macOS and Linux.
    QApplication::setStyle(QStringLiteral("Fusion"));

    QMainWindow* window = build_window();

    if (argc == 3 && std::strcmp(argv[1], "--render") == 0) {
        window->show();
        QApplication::processEvents();
        QPixmap const pixmap = window->grab();
        if (!pixmap.save(QString::fromLocal8Bit(argv[2]), "PNG")) {
            std::fprintf(stderr, "error: could not write %s\n", argv[2]);
            return 1;
        }
        std::printf("rendered %dx%d to %s\n", pixmap.width(), pixmap.height(),
                    argv[2]);
        return 0;
    }

    window->show();
    return app.exec();
}
