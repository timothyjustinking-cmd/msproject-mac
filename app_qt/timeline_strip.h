// app_qt/timeline_strip.h -- the timeline strip above the sheet/Gantt split.
//
// The reference screenshot has one row between the ribbon and the grid: the
// project's Start/Finish at the ends and "Add tasks with dates to the timeline"
// in the middle.  It is a display strip only in wave 17 (adding a task to the
// timeline is Q3's command); the text is painted, the heights are pinned by
// shell_style.h so the split below it starts at the same y on every platform.
#pragma once

#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <vector>

class QPaintEvent;

namespace msqt {

// One bar drawn on the timeline: a task's label and its span as fractions of
// the project's first..last window.  The shell computes the fractions from the
// plan's dates; the strip draws them (dragging comes later).
struct TimelineTask {
    QString label;
    double start = 0.0;
    double width = 0.0;
};

class TimelineStrip final : public QWidget {
  public:
    explicit TimelineStrip(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // The formatted project start/finish (empty hides the end labels).
    void set_dates(const QString& start_text, const QString& finish_text);
    QString centre_text() const;
    const QString& start_text() const noexcept { return start_text_; }
    const QString& finish_text() const noexcept { return finish_text_; }

    // Tasks drawn on the timeline (empty = the prompt is shown).
    void set_tasks(std::vector<TimelineTask> tasks);
    const std::vector<TimelineTask>& tasks() const noexcept { return tasks_; }

    // Wave 20 chrome gate: the Start/Finish block, the timeline bar and the
    // vertical TIMELINE label are real drawn regions, not just centred text.
    QRect start_block_rect() const;
    QRect timeline_bar_rect() const;
    QRect timeline_label_rect() const;
    bool has_date_block() const;
    bool has_timeline_label() const;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QString start_text_;
    QString finish_text_;
    std::vector<TimelineTask> tasks_;
};

}  // namespace msqt
