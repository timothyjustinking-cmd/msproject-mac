// app_qt/gantt_canvas.h -- the Gantt chart: a QWidget that paints core::gantt's
// geometry, and computes none of it.
//
// Every rectangle, header cell, non-working span, link polyline and label
// placement comes from core::gantt::GanttLayout, which app/layout_model.h built
// from the displayed rows.  This file only maps the model's two coordinate
// spaces (bar/row space with y = 0 at the top of row 0, and header space) into
// widget pixels and picks colours: that is the whole drawing contract from
// docs/ui-qt-port.md section 2.3.
//
// No status-date marker is drawn: PlanDocument passes no status date, so the
// offscreen render is deterministic (it must not depend on today's date).
#pragma once

#include <QSize>
#include <QWidget>

#include <cstdint>

#include "gantt.h"

class QPainter;

namespace msqt {

class GanttCanvas final : public QWidget {
  public:
    explicit GanttCanvas(QWidget* parent = nullptr);

    // Replaces the layout after a load, a zoom change, a collapse or an edit.
    void set_layout(const core::gantt::GanttLayout& layout);
    // The bar to outline (0 = nothing selected).
    void set_selected_uid(std::uint32_t uid);
    // Working-day shading on/off (the preference; it never moves a bar).
    void set_shading(bool enabled);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    void draw_header_tier(QPainter& painter, core::gantt::HeaderTier tier, int top,
                          int band_height, int canvas_width);

    core::gantt::GanttLayout layout_;
    std::uint32_t selected_uid_ = 0;
    bool shading_ = true;
};

}  // namespace msqt
