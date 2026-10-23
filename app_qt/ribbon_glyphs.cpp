// app_qt/ribbon_glyphs.cpp -- see ribbon_glyphs.h.
//
// Every glyph is authored on a 32x32 grid with one ink colour and a 2 px
// stroke; shell_ribbon_icon() scales the same drawing to 16 px for the
// one-row controls.  The shapes are geometric marks drawn from the command's
// meaning and the clean ribbon strips' silhouettes -- never Microsoft artwork.
//
// The dispatch is grouped by ribbon tab, one W26-2.<n> item per group.  The
// icon gate hashes the rendered pixels per tab, so a key with no case here (or
// a copy-pasted drawing that makes two commands identical) fails rather than
// showing the placeholder.
#include "ribbon_glyphs.h"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>

#include <initializer_list>

#include "menu_model.h"
#include "shell_icons.h"

namespace msqt {
namespace {

// The one ink colour: every non-transparent pixel of a glyph must be this RGB
// (the icon gate proves that from the rendered image, so it is a rendering
// property, not a reference number).
const QColor kInk(0x1f, 0x3a, 0x5f);

QPen ink_pen(qreal width = 2.0) {
    return QPen(kInk, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
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

void polyline(QPainter& painter, std::initializer_list<QPointF> points, qreal width = 2.0) {
    painter.setPen(ink_pen(width));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(polygon(points));
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

void filled_rect(QPainter& painter, qreal x, qreal y, qreal w, qreal h, qreal radius = 0.0) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(kInk);
    if (radius > 0.0) {
        painter.drawRoundedRect(QRectF(x, y, w, h), radius, radius);
    } else {
        painter.drawRect(QRectF(x, y, w, h));
    }
}

void outline_circle(QPainter& painter, qreal cx, qreal cy, qreal radius,
                    qreal width = 2.0) {
    painter.setPen(ink_pen(width));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(cx, cy), radius, radius);
}

void filled_circle(QPainter& painter, qreal cx, qreal cy, qreal radius) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(kInk);
    painter.drawEllipse(QPointF(cx, cy), radius, radius);
}

void filled_polygon(QPainter& painter, std::initializer_list<QPointF> points) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(kInk);
    painter.drawPolygon(polygon(points));
}

// A head-and-shoulders person mark: the resource commands' common motif.
void person(QPainter& painter, qreal cx, qreal head_y, qreal radius = 3.0) {
    outline_circle(painter, cx, head_y, radius);
    painter.setPen(ink_pen(2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(cx - 2 * radius, head_y + radius + 1, 4 * radius, 2.2 * radius),
                    0, 180 * 16);
}

// A document page with a folded top-right corner.  The base silhouette most of
// the page commands build on.
void page(QPainter& painter, qreal x = 5.0, qreal y = 3.0, qreal w = 22.0, qreal h = 26.0) {
    painter.setPen(ink_pen(2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(polygon({QPointF(x, y), QPointF(x + w - 7, y), QPointF(x + w, y + 7),
                                 QPointF(x + w, y + h), QPointF(x, y + h)}));
    line(painter, x + w - 7, y, x + w - 7, y + 7);
    line(painter, x + w - 7, y + 7, x + w, y + 7);
}

// --- Report tab (W26-2.3) --------------------------------------------------
bool draw_report_glyph(QPainter& painter, const QString& key) {
    if (key == QStringLiteral("project.compare")) {
        page(painter, 3, 5, 12, 22);
        page(painter, 17, 5, 12, 22);
        line(painter, 6, 16, 26, 16, 2.5);
        polyline(painter, {QPointF(10, 12), QPointF(6, 16), QPointF(10, 20)}, 2.5);
        polyline(painter, {QPointF(22, 12), QPointF(26, 16), QPointF(22, 20)}, 2.5);
    } else if (key == QStringLiteral("reports.new")) {
        page(painter);
        filled_rect(painter, 9, 20, 3, 6);
        filled_rect(painter, 13, 16, 3, 10);
        line(painter, 15, 9, 25, 9, 2.5);
        line(painter, 20, 4, 20, 14, 2.5);
    } else if (key == QStringLiteral("reports.dashboards")) {
        page(painter);
        painter.setPen(ink_pen(2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(9, 13, 14, 14), 0, 180 * 16);
        line(painter, 16, 20, 22, 14, 2.5);
        filled_circle(painter, 16, 20, 1.5);
    } else if (key == QStringLiteral("reports.resources")) {
        page(painter);
        person(painter, 18, 17, 3.0);
    } else if (key == QStringLiteral("reports.costs")) {
        page(painter);
        outline_circle(painter, 19, 19, 5.5);
        line(painter, 19, 16, 19, 22);
        line(painter, 16, 19, 22, 19);
    } else if (key == QStringLiteral("reports.in_progress")) {
        page(painter);
        outline_circle(painter, 19, 19, 5.5);
        line(painter, 19, 19, 19, 15);
        line(painter, 19, 19, 22, 21);
    } else if (key == QStringLiteral("reports.getting_started")) {
        page(painter);
        outline_circle(painter, 19, 17, 4);
        line(painter, 17, 22, 21, 22);
        line(painter, 18, 25, 20, 25);
    } else if (key == QStringLiteral("reports.custom")) {
        page(painter);
        line(painter, 12, 24, 22, 14, 3.5);
        filled_polygon(painter, {QPointF(11, 25), QPointF(15, 26), QPointF(12, 22)});
    } else if (key == QStringLiteral("reports.recent")) {
        page(painter);
        painter.setPen(ink_pen(2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(9, 7, 13, 13), 40 * 16, 230 * 16);
        filled_polygon(painter, {QPointF(9, 8), QPointF(15, 8), QPointF(11, 13)});
        outline_circle(painter, 21, 22, 4.5);
        line(painter, 21, 22, 21, 19);
        line(painter, 21, 22, 23, 23);
    } else if (key == QStringLiteral("export.visual_reports")) {
        filled_rect(painter, 4, 21, 4, 8);
        filled_rect(painter, 11, 14, 4, 15);
        filled_rect(painter, 18, 7, 4, 22);
        line(painter, 4, 29, 29, 29, 2.5);
        line(painter, 27, 20, 27, 7, 2.5);
        polyline(painter, {QPointF(23, 11), QPointF(27, 6), QPointF(31, 11)}, 2.5);
    } else {
        return false;
    }
    return true;
}

// --- Resource tab (W26-2.2) ------------------------------------------------
// True when `key` belongs to this tab and was drawn.  Information/Notes/
// Details are Task-tab keys too: Project reuses those glyphs, and the Task
// function above owns them.
bool draw_resource_glyph(QPainter& painter, const QString& key) {
    if (key == QStringLiteral("view.team_planner")) {
        page(painter);
        person(painter, 12, 12, 3.0);
        person(painter, 21, 15, 3.0);
    } else if (key == QStringLiteral("assignments.assign")) {
        person(painter, 11, 14, 3.5);
        person(painter, 21, 16, 3.5);
        line(painter, 21, 5, 29, 5, 2.5);
        line(painter, 25, 1, 25, 9, 2.5);
    } else if (key == QStringLiteral("assignments.pool")) {
        person(painter, 11, 12, 3.5);
        person(painter, 21, 14, 3.5);
        filled_rect(painter, 6, 26, 20, 3);
    } else if (key == QStringLiteral("insert.add_resources")) {
        person(painter, 13, 13, 3.5);
        line(painter, 19, 7, 27, 7, 2.5);
        line(painter, 23, 3, 23, 11, 2.5);
    } else if (key == QStringLiteral("level.selection")) {
        // A short list with a large pointing cursor: the marker, not the rows,
        // is what tells the five Level commands apart at 16 px.
        outline_rect(painter, 3, 7, 13, 4);
        outline_rect(painter, 3, 21, 13, 4);
        filled_polygon(painter, {QPointF(19, 6), QPointF(31, 16), QPointF(25, 17),
                                 QPointF(29, 25), QPointF(25, 27), QPointF(21, 18),
                                 QPointF(17, 17)});
    } else if (key == QStringLiteral("level.resource")) {
        outline_rect(painter, 3, 7, 13, 4);
        outline_rect(painter, 3, 21, 13, 4);
        line(painter, 23, 4, 23, 15, 3);
        filled_polygon(painter, {QPointF(15, 14), QPointF(31, 14), QPointF(23, 28)});
    } else if (key == QStringLiteral("level.all")) {
        outline_rect(painter, 3, 7, 13, 4);
        outline_rect(painter, 3, 14, 13, 4);
        outline_rect(painter, 3, 21, 13, 4);
        line(painter, 25, 5, 25, 27, 3);
        line(painter, 25, 5, 20, 5, 2.5);
        line(painter, 25, 16, 20, 16, 2.5);
        line(painter, 25, 27, 20, 27, 2.5);
    } else if (key == QStringLiteral("level.options")) {
        page(painter);
        outline_circle(painter, 20, 20, 4.5);
        line(painter, 20, 13, 20, 16);
        line(painter, 20, 24, 20, 27);
        line(painter, 13, 20, 16, 20);
        line(painter, 24, 20, 27, 20);
    } else if (key == QStringLiteral("level.clear")) {
        outline_rect(painter, 3, 7, 13, 4);
        outline_rect(painter, 3, 21, 13, 4);
        line(painter, 19, 9, 30, 23, 3);
        line(painter, 30, 9, 19, 23, 3);
    } else if (key == QStringLiteral("level.next_overallocation")) {
        outline_rect(painter, 3, 6, 13, 4);
        outline_rect(painter, 3, 13, 13, 4);
        line(painter, 5, 27, 27, 27, 3);
        filled_polygon(painter, {QPointF(20, 19), QPointF(31, 27), QPointF(20, 31)});
    } else {
        return false;
    }
    return true;
}

// --- Task tab (W26-2.1) ----------------------------------------------------
// True when `key` belongs to this tab and was drawn.
bool draw_task_glyph(QPainter& painter, const QString& key) {
    if (key == QStringLiteral("view.gantt")) {
        page(painter);
        line(painter, 12, 10, 12, 27);
        filled_rect(painter, 14, 13, 9, 3);
        filled_rect(painter, 16, 19, 7, 3);
        filled_rect(painter, 14, 24, 6, 3);
    } else if (key == QStringLiteral("clipboard.paste")) {
        outline_rect(painter, 8, 5, 16, 24, 2);
        filled_rect(painter, 12, 2, 8, 5, 1);
        line(painter, 12, 13, 20, 13);
        line(painter, 12, 18, 20, 18);
    } else if (key == QStringLiteral("clipboard.cut")) {
        outline_circle(painter, 9, 9, 3.5);
        outline_circle(painter, 9, 23, 3.5);
        line(painter, 11.5, 11.5, 26, 24.5);
        line(painter, 11.5, 20.5, 26, 7.5);
    } else if (key == QStringLiteral("clipboard.copy")) {
        outline_rect(painter, 5, 4, 14, 19, 1);
        outline_rect(painter, 11, 9, 15, 19, 1);
        line(painter, 18, 14, 24, 14);
        line(painter, 24, 9, 24, 15);
    } else if (key == QStringLiteral("clipboard.painter")) {
        line(painter, 23, 7, 12, 18, 4);
        filled_polygon(painter, {QPointF(12, 17), QPointF(17, 22), QPointF(7, 27),
                                 QPointF(4, 24)});
    } else if (key == QStringLiteral("font.bold")) {
        polyline(painter, {QPointF(9, 7), QPointF(20, 7), QPointF(23, 10), QPointF(23, 12),
                           QPointF(20, 15), QPointF(9, 15), QPointF(21, 15),
                           QPointF(24, 19), QPointF(24, 22), QPointF(21, 25),
                           QPointF(9, 25), QPointF(9, 7)},
                 2.5);
    } else if (key == QStringLiteral("font.italic")) {
        line(painter, 12, 8, 22, 8);
        line(painter, 18, 8, 14, 24);
        line(painter, 10, 24, 20, 24);
    } else if (key == QStringLiteral("font.underline")) {
        polyline(painter, {QPointF(10, 6), QPointF(10, 17), QPointF(20, 17), QPointF(20, 6)},
                 2.5);
        line(painter, 7, 27, 25, 27, 3);
    } else if (key == QStringLiteral("font.highlight")) {
        line(painter, 23, 6, 13, 16, 5);
        filled_polygon(painter, {QPointF(12, 15), QPointF(17, 20), QPointF(10, 27),
                                 QPointF(5, 22)});
        filled_rect(painter, 7, 28, 18, 2.5);
    } else if (key == QStringLiteral("font.color")) {
        polyline(painter, {QPointF(7, 25), QPointF(16, 5), QPointF(25, 25)});
        line(painter, 11, 18, 21, 18);
        filled_rect(painter, 7, 28, 18, 2.5);
    } else if (key == QStringLiteral("schedule.percent_0") ||
               key == QStringLiteral("schedule.percent_25") ||
               key == QStringLiteral("schedule.percent_50") ||
               key == QStringLiteral("schedule.percent_75") ||
               key == QStringLiteral("schedule.percent_100")) {
        const int percent = key.mid(17).toInt();
        if (percent > 0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(kInk);
            painter.drawPie(QRectF(6, 5, 20, 20), 90 * 16, -percent * 360 * 16 / 100);
        }
        outline_circle(painter, 16, 15, 10);
    } else if (key == QStringLiteral("schedule.mark_on_track")) {
        polyline(painter, {QPointF(6, 17), QPointF(13, 25), QPointF(27, 6)}, 3.5);
    } else if (key == QStringLiteral("schedule.respect_links")) {
        outline_rect(painter, 12, 3, 12, 11, 4);
        outline_rect(painter, 12, 14, 12, 11, 4);
        polyline(painter, {QPointF(22, 21), QPointF(26, 25), QPointF(30, 18)}, 2.5);
    } else if (key == QStringLiteral("schedule.inactivate")) {
        page(painter);
        line(painter, 8, 26, 24, 6, 3);
    } else if (key == QStringLiteral("schedule.outdent")) {
        line(painter, 15, 9, 27, 9);
        line(painter, 15, 23, 27, 23);
        line(painter, 15, 16, 27, 16);
        line(painter, 13, 16, 5, 16);
        polyline(painter, {QPointF(10, 12), QPointF(5, 16), QPointF(10, 20)});
    } else if (key == QStringLiteral("schedule.indent")) {
        line(painter, 9, 9, 27, 9);
        line(painter, 9, 23, 27, 23);
        line(painter, 15, 16, 27, 16);
        line(painter, 5, 16, 13, 16);
        polyline(painter, {QPointF(9, 12), QPointF(13, 16), QPointF(9, 20)});
    } else if (key == QStringLiteral("schedule.link")) {
        outline_rect(painter, 3, 12, 14, 8, 4);
        outline_rect(painter, 15, 12, 14, 8, 4);
    } else if (key == QStringLiteral("schedule.unlink")) {
        outline_rect(painter, 2, 12, 12, 8, 4);
        outline_rect(painter, 18, 12, 12, 8, 4);
        line(painter, 13, 9, 19, 23, 2.5);
    } else if (key == QStringLiteral("schedule.split")) {
        filled_rect(painter, 3, 13, 11, 6);
        filled_rect(painter, 19, 13, 10, 6);
        line(painter, 14, 13, 18, 19);
    } else if (key == QStringLiteral("tasks.manual")) {
        page(painter);
        filled_rect(painter, 18, 19, 9, 7, 2);
        line(painter, 20, 19, 20, 15);
        line(painter, 23, 19, 23, 14);
        line(painter, 26, 19, 26, 16);
        line(painter, 18, 21, 14, 19);
    } else if (key == QStringLiteral("tasks.auto")) {
        page(painter);
        filled_polygon(painter, {QPointF(18, 7), QPointF(26, 7), QPointF(21, 15),
                                 QPointF(26, 15), QPointF(15, 27), QPointF(19, 19),
                                 QPointF(14, 19)});
    } else if (key == QStringLiteral("tasks.inspect")) {
        page(painter);
        outline_circle(painter, 19, 19, 5);
        line(painter, 23, 23, 27, 27, 2.5);
    } else if (key == QStringLiteral("tasks.move")) {
        page(painter);
        line(painter, 11, 20, 28, 20, 2.5);
        polyline(painter, {QPointF(24, 16), QPointF(28, 20), QPointF(24, 24)}, 2.5);
    } else if (key == QStringLiteral("tasks.mode")) {
        page(painter);
        outline_rect(painter, 13, 17, 16, 8, 4);
        filled_circle(painter, 19, 21, 2.5);
        line(painter, 24, 18, 24, 24);
    } else if (key == QStringLiteral("insert.task")) {
        page(painter);
        line(painter, 18, 20, 28, 20, 2.5);
        line(painter, 23, 15, 23, 25, 2.5);
    } else if (key == QStringLiteral("insert.summary")) {
        page(painter);
        line(painter, 12, 10, 24, 10);
        line(painter, 10, 8, 10, 24);
        line(painter, 12, 16, 24, 16);
        line(painter, 12, 22, 24, 22);
    } else if (key == QStringLiteral("insert.milestone")) {
        painter.setPen(ink_pen(2.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(polygon({QPointF(16, 3), QPointF(28, 16), QPointF(16, 29),
                                     QPointF(4, 16)}));
    } else if (key == QStringLiteral("insert.deliverable")) {
        page(painter);
        line(painter, 18, 8, 18, 26);
        filled_polygon(painter, {QPointF(18, 8), QPointF(27, 12), QPointF(18, 16)});
    } else if (key == QStringLiteral("properties.information")) {
        page(painter);
        outline_circle(painter, 20, 19, 5.5);
        filled_circle(painter, 20, 16.5, 1.2);
        line(painter, 20, 19, 20, 23);
    } else if (key == QStringLiteral("properties.notes")) {
        outline_rect(painter, 6, 5, 20, 22, 2);
        filled_polygon(painter, {QPointF(26, 5), QPointF(26, 13), QPointF(18, 5)});
        line(painter, 10, 15, 22, 15);
        line(painter, 10, 20, 22, 20);
    } else if (key == QStringLiteral("properties.details")) {
        page(painter);
        line(painter, 10, 12, 23, 12);
        line(painter, 10, 17, 23, 17);
        line(painter, 10, 22, 20, 22);
    } else if (key == QStringLiteral("properties.add_to_timeline")) {
        page(painter);
        line(painter, 9, 18, 25, 18);
        filled_polygon(painter, {QPointF(12, 18), QPointF(15, 14), QPointF(18, 18),
                                 QPointF(15, 22)});
        filled_polygon(painter, {QPointF(19, 18), QPointF(22, 14), QPointF(25, 18),
                                 QPointF(22, 22)});
    } else if (key == QStringLiteral("editing.scroll_to_task")) {
        page(painter);
        line(painter, 23, 9, 23, 25);
        line(painter, 14, 10, 14, 21);
        polyline(painter, {QPointF(10, 17), QPointF(14, 23), QPointF(18, 17)});
    } else if (key == QStringLiteral("editing.find")) {
        outline_circle(painter, 13, 13, 8);
        line(painter, 19, 19, 28, 28, 3);
    } else if (key == QStringLiteral("editing.clear")) {
        line(painter, 23, 7, 14, 16, 4);
        filled_polygon(painter, {QPointF(13, 15), QPointF(18, 20), QPointF(9, 27),
                                 QPointF(6, 24)});
        line(painter, 4, 5, 10, 11, 2.5);
        line(painter, 10, 5, 4, 11, 2.5);
    } else if (key == QStringLiteral("editing.fill")) {
        painter.setPen(ink_pen(2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(polygon({QPointF(7, 15), QPointF(16, 6), QPointF(26, 16),
                                     QPointF(17, 25)}));
        line(painter, 10, 12, 21, 23);
        filled_circle(painter, 25, 26, 2.5);
    } else {
        return false;
    }
    return true;
}

}  // namespace

QIcon shell_ribbon_icon(const QString& key, int pixels) {
    if (pixels != 16 && pixels != 32) {
        pixels = 16;
    }
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(static_cast<double>(pixels) / 32.0, static_cast<double>(pixels) / 32.0);
    painter.setPen(ink_pen(2.0));
    painter.setBrush(Qt::NoBrush);
    if (!draw_task_glyph(painter, key) && !draw_resource_glyph(painter, key) &&
        !draw_report_glyph(painter, key)) {
        painter.end();
        // A key with no drawing is the neutral placeholder, never a blank
        // icon: the gate's placeholder hash then fails on it.
        return shell_action_icon(app::MenuAction::none, pixels);
    }
    painter.end();
    return QIcon(pixmap);
}

bool shell_ribbon_glyph_known(const QString& key) {
    QPixmap probe(32, 32);
    probe.fill(Qt::transparent);
    QPainter painter(&probe);
    painter.setPen(ink_pen(2.0));
    painter.setBrush(Qt::NoBrush);
    const bool known = draw_task_glyph(painter, key) || draw_resource_glyph(painter, key) ||
                       draw_report_glyph(painter, key);
    painter.end();
    return known;
}

}  // namespace msqt
