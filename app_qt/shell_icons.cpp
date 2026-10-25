// app_qt/shell_icons.cpp -- see shell_icons.h.
//
// WAVE 29 (W29-5.2): the menu/QAT/status-bar artwork is drawn in the wave-26
// one-ink style -- a 32 px grid scaled to the requested size, round strokes,
// and ONE ink per render (decision 3: the port's own monochrome glyphs, never
// Microsoft artwork).  The caller passes the live chrome ink, so the same
// drawing is legible on the light ribbon, Dark Gray and Black.

#include "shell_icons.h"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSize>

#include <initializer_list>

namespace msqt {
namespace {

// The wave-26 style ink: the light theme's ribbon ink.  An invalid caller ink
// falls back to it rather than drawing black.
const QColor kStyleInk(0x1f, 0x3a, 0x5f);
QColor g_ink = kStyleInk;  // the ink the glyph helpers are drawing with now

QPen ink_pen(qreal width = 2.0) {
    return QPen(g_ink, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

QPolygonF polygon(std::initializer_list<QPointF> points) {
    QPolygonF result;
    for (const QPointF& point : points) {
        result.append(point);
    }
    return result;
}

void line(QPainter& painter, qreal x1, qreal y1, qreal x2, qreal y2, qreal width = 2.0) {
    painter.setPen(ink_pen(width));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
}

void outline_rect(QPainter& painter, qreal x, qreal y, qreal w, qreal h,
                  qreal radius = 0.0, qreal width = 2.0) {
    painter.setPen(ink_pen(width));
    painter.setBrush(Qt::NoBrush);
    if (radius > 0.0) {
        painter.drawRoundedRect(QRectF(x, y, w, h), radius, radius);
    } else {
        painter.drawRect(QRectF(x, y, w, h));
    }
}

void filled_rect(QPainter& painter, qreal x, qreal y, qreal w, qreal h) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(g_ink);
    painter.drawRect(QRectF(x, y, w, h));
}

void outline_circle(QPainter& painter, qreal cx, qreal cy, qreal radius,
                    qreal width = 2.0) {
    painter.setPen(ink_pen(width));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(cx, cy), radius, radius);
}

void filled_circle(QPainter& painter, qreal cx, qreal cy, qreal radius) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(g_ink);
    painter.drawEllipse(QPointF(cx, cy), radius, radius);
}

void outline_polygon(QPainter& painter, std::initializer_list<QPointF> points) {
    painter.setPen(ink_pen(2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(polygon(points));
}

// A sheet/page outline -- the base of the page-shaped commands.
void page(QPainter& painter, qreal x, qreal y, qreal w, qreal h) {
    outline_rect(painter, x, y, w, h, 1.0);
}

// The plus/minus marks, wide enough to stay solid at 16 px.
void plus(QPainter& painter) {
    line(painter, 16, 6, 16, 26, 3.0);
    line(painter, 6, 16, 26, 16, 3.0);
}

void minus(QPainter& painter) {
    line(painter, 6, 16, 26, 16, 3.0);
}

// A magnifier with a plus or a minus in its lens.
void magnifier(QPainter& painter, bool zoom_in) {
    outline_circle(painter, 14, 14, 8);
    line(painter, 20, 20, 27, 27, 2.5);
    line(painter, 9, 14, 19, 14);
    if (zoom_in) {
        line(painter, 14, 9, 14, 19);
    }
}

// A calendar page with the day/week/month divisions.
void calendar(QPainter& painter, int divisions) {
    outline_rect(painter, 4, 6, 24, 20, 1.0);
    line(painter, 4, 12, 28, 12);
    for (int index = 1; index <= divisions; ++index) {
        const qreal x = 4.0 + 24.0 * index / (divisions + 1);
        line(painter, x, 14, x, 24);
    }
}

// The neutral placeholder: a rounded tile with a dot.  It is intentionally
// part of the one-ink set; the icon gate still rejects it as a command glyph.
void placeholder(QPainter& painter) {
    outline_rect(painter, 7, 7, 18, 18, 3.0);
    filled_circle(painter, 16, 16, 2.5);
}

}  // namespace

QIcon shell_action_icon(app::MenuAction action, int pixels, const QColor& ink) {
    if (pixels <= 0) {
        pixels = kShellIconPx;
    }
    g_ink = ink.isValid() ? ink : kStyleInk;
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // The glyphs are authored on a 32 px grid; a 16 px request is the same
    // drawing at half size, exactly like the ribbon's one-ink set.
    painter.scale(static_cast<double>(pixels) / 32.0,
                  static_cast<double>(pixels) / 32.0);
    painter.setPen(ink_pen(2.0));
    painter.setBrush(Qt::NoBrush);
    switch (action) {
        case app::MenuAction::save:
        case app::MenuAction::save_as: {
            // A floppy disk: shutter at the top, label at the bottom.
            outline_rect(painter, 7, 3, 18, 26, 2.0);
            outline_rect(painter, 12, 3, 8, 9, 1.0);
            outline_rect(painter, 12, 19, 8, 10, 1.0);
            break;
        }
        case app::MenuAction::open: {
            outline_polygon(painter, {QPointF(4, 12), QPointF(13, 12), QPointF(16, 15),
                                      QPointF(28, 15), QPointF(28, 26), QPointF(4, 26)});
            break;
        }
        case app::MenuAction::undo:
        case app::MenuAction::redo: {
            // A return arrow: the head points the way the history moves (left
            // for Undo, right for Redo).  A straight shaft stays legible at
            // 16 px, where a curved hook collapses into a tick.
            const bool redo = action == app::MenuAction::redo;
            const qreal tip = redo ? 24.0 : 8.0;
            const qreal tail = redo ? 5.0 : 27.0;
            line(painter, tip, 14, tail, 14, 2.5);
            line(painter, tip, 14, tip + (redo ? -7.0 : 7.0), 7, 2.5);
            line(painter, tip, 14, tip + (redo ? -7.0 : 7.0), 21, 2.5);
            break;
        }
        case app::MenuAction::copy: {
            outline_rect(painter, 5, 3, 15, 20, 1.0);
            outline_rect(painter, 12, 9, 15, 20, 1.0);
            break;
        }
        case app::MenuAction::cut: {
            // Scissors: two rings below two crossing blades.
            outline_circle(painter, 8, 25, 3);
            outline_circle(painter, 14, 25, 3);
            line(painter, 10, 22, 27, 6);
            line(painter, 16, 22, 3, 9);
            break;
        }
        case app::MenuAction::paste: {
            outline_rect(painter, 7, 7, 18, 22, 2.0);
            outline_rect(painter, 12, 3, 8, 6, 1.0);
            line(painter, 11, 16, 21, 16);
            line(painter, 11, 21, 21, 21);
            break;
        }
        case app::MenuAction::insert_task: {
            plus(painter);
            break;
        }
        case app::MenuAction::delete_task: {
            minus(painter);
            break;
        }
        case app::MenuAction::custom_fields: {
            // A page ruled as a table.
            page(painter, 7, 4, 18, 24);
            line(painter, 11, 10, 11, 22);
            line(painter, 11, 10, 21, 10);
            line(painter, 11, 14, 21, 14);
            line(painter, 11, 18, 21, 18);
            line(painter, 11, 22, 21, 22);
            break;
        }
        case app::MenuAction::task_fields: {
            // A page with an information mark.
            page(painter, 7, 4, 18, 24);
            filled_circle(painter, 16, 13, 1.6);
            line(painter, 16, 17, 16, 23, 2.5);
            break;
        }
        case app::MenuAction::predecessors: {
            // A page with two linked nodes.
            page(painter, 7, 4, 18, 24);
            outline_circle(painter, 12, 14, 2.5);
            outline_circle(painter, 21, 21, 2.5);
            line(painter, 14, 16, 19, 20);
            break;
        }
        case app::MenuAction::custom_field_formula: {
            // A page with an equals mark.
            page(painter, 7, 4, 18, 24);
            line(painter, 11, 14, 21, 14);
            line(painter, 11, 19, 21, 19);
            break;
        }
        case app::MenuAction::zoom_in: {
            magnifier(painter, true);
            break;
        }
        case app::MenuAction::zoom_out: {
            magnifier(painter, false);
            break;
        }
        case app::MenuAction::zoom_day: {
            calendar(painter, 5);
            break;
        }
        case app::MenuAction::zoom_week: {
            calendar(painter, 2);
            break;
        }
        case app::MenuAction::zoom_month: {
            calendar(painter, 1);
            break;
        }
        case app::MenuAction::print:
        case app::MenuAction::save_as_pdf:
        case app::MenuAction::export_pdf_sheet:
        case app::MenuAction::export_pdf_gantt: {
            // A printer: paper above, body below, inked output slot.
            outline_rect(painter, 10, 4, 12, 8, 1.0);
            outline_rect(painter, 5, 12, 22, 14, 1.0);
            filled_rect(painter, 10, 19, 12, 4);
            break;
        }
        case app::MenuAction::show_filter:
        case app::MenuAction::clear_filter: {
            outline_polygon(painter, {QPointF(5, 6), QPointF(27, 6), QPointF(19, 16),
                                      QPointF(19, 27), QPointF(14, 23), QPointF(14, 16)});
            break;
        }
        case app::MenuAction::preferences: {
            // A gear: ring, hub and four teeth.
            outline_circle(painter, 16, 16, 9);
            filled_circle(painter, 16, 16, 3);
            line(painter, 16, 4, 16, 8);
            line(painter, 16, 24, 16, 28);
            line(painter, 4, 16, 8, 16);
            line(painter, 24, 16, 28, 16);
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
            placeholder(painter);
            break;
        }
    }
    painter.end();
    return QIcon(pixmap);
}

QIcon shell_view_icon(int view_mode, int pixels, const QColor& ink) {
    if (pixels <= 0) {
        pixels = kShellIconPx;
    }
    g_ink = ink.isValid() ? ink : kStyleInk;
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(static_cast<double>(pixels) / 32.0,
                  static_cast<double>(pixels) / 32.0);
    // Every view glyph shares the sheet frame; the interior mark distinguishes
    // the view, exactly as the earlier two-colour set did.
    outline_rect(painter, 4, 6, 24, 20, 1.0);
    switch (view_mode) {
        case 0:  // Gantt Chart: a table column plus two bars.
            line(painter, 13, 6, 13, 26);
            filled_rect(painter, 15, 11, 8, 3);
            filled_rect(painter, 16, 18, 7, 3);
            break;
        case 1:  // Task Usage: a ruled grid.
            line(painter, 4, 13, 28, 13);
            line(painter, 4, 20, 28, 20);
            line(painter, 20, 6, 20, 26);
            break;
        case 2:  // Team Planner: two resource lanes with a block each.
            line(painter, 4, 16, 28, 16);
            filled_rect(painter, 7, 9, 8, 3);
            filled_rect(painter, 16, 20, 8, 3);
            break;
        case 3:  // Resource Sheet: a list.
            line(painter, 8, 12, 24, 12);
            line(painter, 8, 17, 24, 17);
            line(painter, 8, 22, 24, 22);
            break;
        case 4:  // Report: a page with a bar chart.
            line(painter, 8, 24, 24, 24);
            filled_rect(painter, 9, 17, 4, 7);
            filled_rect(painter, 15, 10, 4, 14);
            filled_rect(painter, 21, 14, 4, 10);
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
    painter.drawRoundedRect(QRectF(1, 1, 30, 30), 5, 5);
    painter.setBrush(QColor(0xff, 0xff, 0xff));
    painter.drawRect(QRectF(7, 9, 4, 14));
    painter.drawRect(QRectF(14, 13, 4, 10));
    painter.drawRect(QRectF(21, 6, 4, 17));
    painter.end();
    return QIcon(pixmap);
}

}  // namespace msqt
