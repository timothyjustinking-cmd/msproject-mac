// app_qt/timeline_strip.cpp -- see timeline_strip.h.

#include "timeline_strip.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QSize>
#include <QSizePolicy>

#include "shell_style.h"

namespace msqt {
namespace {

const QColor kTimelineText(0x3a, 0x3a, 0x3a);
const QColor kTimelineMuted(0x6e, 0x6e, 0x6e);

QFont label_font(int point_size) {
    QFont font;
    font.setPointSize(point_size);
    return font;
}

}  // namespace

TimelineStrip::TimelineStrip(QWidget* parent) : QWidget(parent) {
    setFixedHeight(kTimelineHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAutoFillBackground(false);
}

QSize TimelineStrip::sizeHint() const { return QSize(900, kTimelineHeight); }

QSize TimelineStrip::minimumSizeHint() const { return QSize(200, kTimelineHeight); }

void TimelineStrip::set_dates(const QString& start_text, const QString& finish_text) {
    start_text_ = start_text;
    finish_text_ = finish_text;
    update();
}

QString TimelineStrip::centre_text() const {
    return QStringLiteral("Add tasks with dates to the timeline");
}

void TimelineStrip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kTimelineBackground);
    painter.setPen(QPen(kRibbonBorder, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    const int pad = 12;
    const int band = height() - 1;
    const QRect text_band(0, 2, width(), band - 2);

    painter.setFont(label_font(kStatusPointSize));
    painter.setPen(kTimelineMuted);
    if (!start_text_.isEmpty()) {
        painter.drawText(QRect(pad, 2, 40, band - 2), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Start"));
        painter.drawText(QRect(pad + 40, 2, width() / 2, band - 2),
                         Qt::AlignLeft | Qt::AlignVCenter, start_text_);
    }
    if (!finish_text_.isEmpty()) {
        const int right = width() - pad;
        painter.drawText(QRect(right - 40, 2, 40, band - 2),
                         Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("Finish"));
        painter.drawText(QRect(right - 40 - 170, 2, 165, band - 2),
                         Qt::AlignRight | Qt::AlignVCenter, finish_text_);
    }

    painter.setFont(label_font(kBodyPointSize));
    painter.setPen(kTimelineText);
    painter.drawText(text_band, Qt::AlignCenter, centre_text());
}

}  // namespace msqt
