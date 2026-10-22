// app_qt/timeline_strip.h -- the timeline strip above the sheet/Gantt split.
//
// The reference screenshot has one row between the ribbon and the grid: the
// project's Start/Finish at the ends and "Add tasks with dates to the timeline"
// in the middle.  It is a display strip only in wave 17 (adding a task to the
// timeline is Q3's command); the text is painted, the heights are pinned by
// shell_style.h so the split below it starts at the same y on every platform.
#pragma once

#include <QString>
#include <QWidget>

class QPaintEvent;

namespace msqt {

class TimelineStrip final : public QWidget {
  public:
    explicit TimelineStrip(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // The formatted project start/finish (empty hides the end labels).
    void set_dates(const QString& start_text, const QString& finish_text);
    QString centre_text() const;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QString start_text_;
    QString finish_text_;
};

}  // namespace msqt
