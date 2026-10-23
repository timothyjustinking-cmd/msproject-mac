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
#include <QString>

#include <algorithm>
#include <utility>
#include <vector>

#include "shell_style.h"

namespace msqt {
namespace {

const QColor kTimelineText(0x3a, 0x3a, 0x3a);
const QColor kTimelineMuted(0x6e, 0x6e, 0x6e);
const QColor kBlockBackground(0xfa, 0xfa, 0xfa);
const QColor kBlockBorder(0xd0, 0xd0, 0xd0);
const QColor kBarBorder(0xc4, 0xc4, 0xc4);
const QColor kTaskBar(0x4a, 0x7e, 0xbb);

constexpr int kPad = 8;
constexpr int kBlockWidth = 230;
constexpr int kBarGap = 10;

QFont label_font(int point_size, bool bold = false) {
    QFont font;
    font.setPointSize(point_size);
    font.setBold(bold);
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

void TimelineStrip::set_tasks(std::vector<TimelineTask> tasks) {
    tasks_ = std::move(tasks);
    update();
}

QString TimelineStrip::centre_text() const {
    return QStringLiteral("Add tasks with dates to the timeline");
}

QRect TimelineStrip::timeline_label_rect() const {
    return QRect(0, 1, kTimelineLabelWidth, height() - 2);
}

QRect TimelineStrip::start_block_rect() const {
    const int left = kTimelineLabelWidth + kPad;
    return QRect(left, 5, kBlockWidth, height() - 11);
}

QRect TimelineStrip::timeline_bar_rect() const {
    const QRect block = start_block_rect();
    const int left = block.right() + 1 + kBarGap;
    return QRect(left, 7, width() - left - kPad, height() - 15);
}

bool TimelineStrip::has_date_block() const {
    const QRect block = start_block_rect();
    return block.isValid() && block.width() >= 160 && block.height() >= 24;
}

bool TimelineStrip::has_timeline_label() const {
    const QRect label = timeline_label_rect();
    return label.isValid() && label.width() >= 16 && label.height() >= 30;
}

void TimelineStrip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kTimelineBackground);
    painter.setPen(QPen(kRibbonBorder, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    // The vertical TIMELINE label at the strip's left edge.
    const QRect label = timeline_label_rect();
    painter.fillRect(label, QColor(0xf2, 0xf2, 0xf2));
    painter.setPen(QPen(kBlockBorder, 1));
    painter.drawLine(label.right(), label.top(), label.right(), label.bottom());
    painter.save();
    painter.translate(label.center().x(), label.center().y());
    painter.rotate(-90.0);
    painter.setFont(label_font(kStatusPointSize - 1, true));
    painter.setPen(kTimelineMuted);
    painter.drawText(QRect(-60, -10, 120, 20), static_cast<int>(Qt::AlignCenter),
                     QStringLiteral("TIMELINE"));
    painter.restore();

    // The Start/Finish block: two labelled lines, today for a new project.
    const QRect block = start_block_rect();
    painter.setPen(QPen(kBlockBorder, 1));
    painter.setBrush(kBlockBackground);
    painter.drawRoundedRect(block, 2, 2);
    painter.setFont(label_font(kStatusPointSize - 1, true));
    painter.setPen(kTimelineMuted);
    painter.drawText(QRect(block.left() + 8, block.top() + 2, 44, 12),
                     static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                     QStringLiteral("Start"));
    painter.drawText(QRect(block.left() + 8, block.top() + 16, 44, 12),
                     static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                     QStringLiteral("Finish"));
    painter.setFont(label_font(kStatusPointSize));
    painter.setPen(kTimelineText);
    painter.drawText(QRect(block.left() + 54, block.top() + 2, block.width() - 62, 12),
                     static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                     start_text_.isEmpty() ? QStringLiteral("--") : start_text_);
    painter.drawText(QRect(block.left() + 54, block.top() + 16, block.width() - 62, 12),
                     static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                     finish_text_.isEmpty() ? QStringLiteral("--") : finish_text_);

    // The timeline itself: a rounded bar.  Tasks added to the timeline are drawn
    // as small bars at their fraction of the project window; an empty project
    // shows the product's centred prompt.
    const QRect bar = timeline_bar_rect();
    painter.setPen(QPen(kBarBorder, 1));
    painter.setBrush(QColor(0xff, 0xff, 0xff));
    painter.drawRoundedRect(bar, 8, 8);
    if (tasks_.empty()) {
        painter.setPen(kTimelineText);
        painter.setFont(label_font(kBodyPointSize));
        painter.drawText(bar, static_cast<int>(Qt::AlignCenter), centre_text());
    } else {
        painter.setClipRect(bar.adjusted(2, 2, -2, -2));
        painter.setPen(Qt::NoPen);
        painter.setBrush(kTaskBar);
        const int top = bar.top() + 6;
        const int task_height = std::max(6, (bar.height() - 12) / 3);
        int row = 0;
        for (const TimelineTask& task : tasks_) {
            const double start = std::clamp(task.start, 0.0, 1.0);
            const double width = std::clamp(task.width, 0.002, 1.0 - start);
            const int left = bar.left() + static_cast<int>(start * bar.width());
            const int span = std::max(3, static_cast<int>(width * bar.width()));
            const int y = top + (row % 3) * (task_height + 1);
            painter.drawRoundedRect(QRect(left, y, span, task_height), 2, 2);
            ++row;
        }
        painter.setClipping(false);
    }
}

}  // namespace msqt
