// app_qt/shell_icons.cpp -- see shell_icons.h.

#include "shell_icons.h"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QPolygon>
#include <QRect>
#include <QSize>
#include <QString>

namespace msqt {
namespace {

const QColor kInk(0x2b, 0x4a, 0x6f);
const QColor kAccent(0x2f, 0x6b, 0xa8);
const QColor kPaper(0xff, 0xff, 0xff);
const QColor kMuted(0x8a, 0x8a, 0x8a);
const QColor kRed(0xb0, 0x3a, 0x2e);
const QColor kGreen(0x2f, 0x6b, 0x3a);

QPixmap canvas(int pixels) {
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

void draw_page(QPainter& painter, const QRect& rect) {
    painter.setPen(QPen(kInk, 1));
    painter.setBrush(kPaper);
    painter.drawRect(rect);
    painter.drawLine(rect.left() + 2, rect.top() + 4, rect.right() - 2, rect.top() + 4);
    painter.drawLine(rect.left() + 2, rect.top() + 6, rect.right() - 3, rect.top() + 6);
}

}  // namespace

QIcon shell_action_icon(app::MenuAction action, int pixels) {
    if (pixels <= 0) {
        pixels = kShellIconPx;
    }
    QPixmap pixmap = canvas(pixels);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // The glyphs are authored on a 16 px grid; a 32 px ribbon icon is the same
    // drawing at 2x so it stays crisp without a second artwork set.
    const double scale = static_cast<double>(pixels) / static_cast<double>(kShellIconPx);
    painter.scale(scale, scale);
    switch (action) {
        case app::MenuAction::save:
        case app::MenuAction::save_as: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(QColor(0xd6, 0xe4, 0xf2));
            painter.drawRoundedRect(QRect(2, 1, 12, 14), 1, 1);
            painter.setBrush(kAccent);
            painter.drawRect(QRect(5, 2, 6, 4));
            painter.setBrush(kPaper);
            painter.drawRect(QRect(4, 9, 8, 5));
            break;
        }
        case app::MenuAction::open: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(QColor(0xf3, 0xd9, 0x9a));
            QPolygon folder;
            folder << QPoint(1, 4) << QPoint(6, 4) << QPoint(7, 6) << QPoint(15, 6)
                   << QPoint(15, 13) << QPoint(1, 13);
            painter.drawPolygon(folder);
            break;
        }
        case app::MenuAction::undo:
        case app::MenuAction::redo: {
            const bool redo = action == app::MenuAction::redo;
            painter.setPen(QPen(kAccent, 2));
            painter.setBrush(Qt::NoBrush);
            const QRect arc(2, 4, 12, 9);
            painter.drawArc(arc, redo ? 20 * 16 : 160 * 16, 120 * 16);
            painter.setPen(QPen(kAccent, 2));
            const int x = redo ? 12 : 3;
            painter.drawLine(x, 3, x, 7);
            painter.drawLine(x, 3, x + (redo ? 3 : -3), 5);
            break;
        }
        case app::MenuAction::copy: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(kPaper);
            painter.drawRect(QRect(2, 2, 8, 10));
            painter.drawRect(QRect(5, 4, 8, 10));
            break;
        }
        case app::MenuAction::cut: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(kPaper);
            painter.drawEllipse(QPoint(4, 12), 2, 2);
            painter.drawEllipse(QPoint(11, 12), 2, 2);
            painter.setPen(QPen(kRed, 1));
            painter.drawLine(5, 10, 12, 2);
            painter.drawLine(10, 10, 3, 2);
            break;
        }
        case app::MenuAction::paste: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(kPaper);
            painter.drawRect(QRect(3, 3, 10, 11));
            painter.setBrush(QColor(0xe8, 0xee, 0xf5));
            painter.drawRect(QRect(5, 1, 6, 3));
            break;
        }
        case app::MenuAction::insert_task: {
            painter.setPen(QPen(kGreen, 2));
            painter.drawLine(8, 2, 8, 14);
            painter.drawLine(2, 8, 14, 8);
            break;
        }
        case app::MenuAction::delete_task: {
            painter.setPen(QPen(kRed, 2));
            painter.drawLine(3, 8, 13, 8);
            break;
        }
        case app::MenuAction::task_fields:
        case app::MenuAction::predecessors:
        case app::MenuAction::custom_field_formula: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(kPaper);
            draw_page(painter, QRect(2, 2, 12, 12));
            if (action == app::MenuAction::predecessors) {
                painter.setPen(QPen(kAccent, 2));
                painter.drawLine(4, 10, 8, 6);
                painter.drawLine(8, 6, 12, 10);
            } else {
                painter.setPen(QPen(kMuted, 1));
                painter.drawText(QRect(2, 2, 12, 12), Qt::AlignCenter, QStringLiteral("fx"));
            }
            break;
        }
        case app::MenuAction::zoom_in:
        case app::MenuAction::zoom_out: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPoint(6, 6), 4, 4);
            painter.drawLine(9, 9, 14, 14);
            painter.setPen(QPen(kAccent, 1));
            painter.drawLine(3, 6, 9, 6);
            if (action == app::MenuAction::zoom_in) {
                painter.drawLine(6, 3, 6, 9);
            }
            break;
        }
        case app::MenuAction::zoom_day:
        case app::MenuAction::zoom_week:
        case app::MenuAction::zoom_month: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(kPaper);
            painter.drawRect(QRect(2, 3, 12, 10));
            painter.setPen(QPen(kAccent, 1));
            if (action == app::MenuAction::zoom_day) {
                for (int x = 3; x < 14; x += 2) {
                    painter.drawLine(x, 4, x, 12);
                }
            } else if (action == app::MenuAction::zoom_week) {
                for (int x = 3; x < 14; x += 5) {
                    painter.drawLine(x, 4, x, 12);
                }
            } else {
                painter.drawLine(7, 4, 7, 12);
            }
            break;
        }
        case app::MenuAction::print:
        case app::MenuAction::save_as_pdf:
        case app::MenuAction::export_pdf_sheet:
        case app::MenuAction::export_pdf_gantt: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(kPaper);
            painter.drawRect(QRect(3, 6, 10, 7));
            painter.drawRect(QRect(5, 1, 6, 5));
            painter.setBrush(kAccent);
            painter.drawRect(QRect(5, 9, 6, 2));
            break;
        }
        case app::MenuAction::show_filter:
        case app::MenuAction::clear_filter: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(QColor(0xe3, 0xec, 0xf5));
            QPolygon funnel;
            funnel << QPoint(2, 2) << QPoint(14, 2) << QPoint(9, 8) << QPoint(9, 14)
                   << QPoint(7, 12) << QPoint(7, 8);
            painter.drawPolygon(funnel);
            break;
        }
        case app::MenuAction::preferences: {
            painter.setPen(QPen(kInk, 1));
            painter.setBrush(kPaper);
            painter.drawEllipse(QPoint(8, 8), 6, 6);
            painter.setBrush(kAccent);
            painter.drawEllipse(QPoint(8, 8), 2, 2);
            break;
        }
        case app::MenuAction::about:
        case app::MenuAction::keyboard_shortcuts:
        case app::MenuAction::toggle_shading:
        case app::MenuAction::collapse_all:
        case app::MenuAction::expand_all:
        case app::MenuAction::select_all:
        case app::MenuAction::view_task_sheet:
        case app::MenuAction::view_resource_sheet:
        case app::MenuAction::view_task_usage:
        case app::MenuAction::view_resource_usage:
        case app::MenuAction::usage_day:
        case app::MenuAction::usage_week:
        case app::MenuAction::usage_month:
        case app::MenuAction::table_date_format:
        case app::MenuAction::insert_resource:
        case app::MenuAction::hide_app:
        case app::MenuAction::quit_app:
        case app::MenuAction::close_window:
        case app::MenuAction::none:
        default: {
            painter.setPen(QPen(kMuted, 1));
            painter.setBrush(QColor(0xee, 0xee, 0xee));
            painter.drawRoundedRect(QRect(2, 2, 12, 12), 2, 2);
            painter.setBrush(kAccent);
            painter.drawEllipse(QPoint(8, 8), 2, 2);
            break;
        }
    }
    painter.end();
    return QIcon(pixmap);
}

QIcon shell_view_icon(int view_mode) {
    QPixmap pixmap = canvas(kShellIconPx);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(kInk, 1));
    painter.setBrush(kPaper);
    painter.drawRect(QRect(1, 2, 14, 12));
    painter.setPen(QPen(kAccent, 1));
    switch (view_mode) {
        case 0:  // Gantt chart: a table column plus bars.
            painter.drawLine(5, 2, 5, 14);
            painter.setBrush(kAccent);
            painter.drawRect(QRect(6, 5, 5, 2));
            painter.drawRect(QRect(7, 9, 5, 2));
            break;
        case 1:  // Task usage: a grid with an extra divider.
            painter.drawLine(2, 6, 14, 6);
            painter.drawLine(2, 10, 14, 10);
            painter.drawLine(9, 2, 9, 14);
            break;
        case 2:  // Team planner: two resource lanes with a block each.
            painter.drawLine(2, 7, 14, 7);
            painter.setBrush(kAccent);
            painter.drawRect(QRect(4, 4, 3, 2));
            painter.drawRect(QRect(9, 9, 3, 2));
            break;
        case 3:  // Resource sheet: a list.
            for (int y = 4; y <= 12; y += 4) {
                painter.drawLine(2, y, 14, y);
            }
            break;
        case 4:  // Report: a page with a bar chart.
            painter.drawLine(4, 12, 12, 12);
            painter.setBrush(kAccent);
            painter.drawRect(QRect(5, 8, 2, 4));
            painter.drawRect(QRect(8, 5, 2, 7));
            painter.drawRect(QRect(11, 7, 1, 5));
            break;
        default:
            break;
    }
    painter.end();
    return QIcon(pixmap);
}

QIcon shell_app_icon() {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x1f, 0x5c, 0x99));
    painter.drawRoundedRect(QRect(1, 1, 30, 30), 5, 5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kPaper);
    painter.drawRect(QRect(7, 9, 4, 14));
    painter.drawRect(QRect(14, 13, 4, 10));
    painter.drawRect(QRect(21, 6, 4, 17));
    painter.end();
    return QIcon(pixmap);
}

}  // namespace msqt
