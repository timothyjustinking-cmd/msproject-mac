// app_qt/view_label.h -- the rotated view caption at the chart's left edge.
//
// The reference shell has a narrow vertical strip reading the view's name
// alongside the chart ("GANTT CHART" for the task view).  It is painted, not a
// platform label: one QPainter rotation and one fixed width, so the sheet's
// measured 60 px gutter and 819 px Entry span are untouched (the strip lives
// inside the chart pane, after the splitter).
#pragma once

#include <QSize>
#include <QString>
#include <QWidget>

class QPaintEvent;

namespace msqt {

class ViewLabel final : public QWidget {
  public:
    explicit ViewLabel(const QString& text, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    const QString& text() const noexcept { return text_; }

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QString text_;
};

}  // namespace msqt
