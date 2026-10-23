// app_qt/tests/gate_util.h -- the tiny assertion toolkit the wave-20 structural
// gates share.  Each gate is its own executable and includes this header once.
#pragma once

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QRect>
#include <QString>
#include <QWidget>

#include <cmath>
#include <cstdio>

#include "menu_model.h"

namespace gate {

inline int failures = 0;

inline void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

inline void check_near(double value, double target, double tolerance, const char* what) {
    if (std::fabs(value - target) > tolerance) {
        std::fprintf(stderr, "FAIL: %s (got %.3f, want %.3f +/- %.3f)\n", what, value, target,
                     tolerance);
        ++failures;
    }
}

inline QAction* find_action(QMainWindow& window, app::MenuAction id) {
    QMenuBar* bar = window.menuBar();
    if (bar == nullptr) {
        return nullptr;
    }
    for (QAction* menu_action : bar->actions()) {
        QMenu* menu = menu_action->menu();
        if (menu == nullptr) {
            continue;
        }
        for (QAction* action : menu->actions()) {
            if (!action->data().isValid()) {
                continue;
            }
            if (action->data().toInt() == static_cast<int>(id)) {
                return action;
            }
        }
    }
    return nullptr;
}

// True when `inner` lies wholly inside `outer` (one pixel of slack).
inline bool inside(const QRect& outer, const QRect& inner) {
    return inner.left() >= outer.left() - 1 && inner.top() >= outer.top() - 1 &&
           inner.right() <= outer.right() + 1 && inner.bottom() <= outer.bottom() + 1;
}

inline int finish() {
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace gate
