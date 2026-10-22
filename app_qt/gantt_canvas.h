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
// Wave 17 Q2 puts the reference's two-tier timescale here (docs/parity.md):
//   * tier 0 (top, 21 px): week labels, "12 Oct '26", one per week;
//   * tier 1 (bottom, 21 px): single-letter day cells S M T W T F S, weekends
//     shaded;
// at 24 px per day (shell_style.h kPixelsPerDay), with month and week boundaries
// emphasised.  The layout's Timescale still owns date<->x (app::
// timescale_for_zoom sets it in PlanDocument); this class only draws.
//
// No status-date marker is drawn: PlanDocument passes no status date, so the
// offscreen render is deterministic (it must not depend on today's date).
#pragma once

#include <QSize>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

#include "gantt.h"

class QPainter;
class QPaintEvent;

namespace msqt {

// What the painted header is made of, for the offscreen parity test.  All values
// are computed from the layout's timescale and the same helpers paintEvent uses.
struct GanttHeaderInfo {
    int header_height = 0;
    int tier_height = 0;
    double day_width = 0.0;
    int major_cells = 0;  // week labels
    int minor_cells = 0;  // day cells
    std::string first_major_label;
    std::string first_minor_label;
};

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

    // Geometry of the two-tier header for a widget of `width` pixels.
    GanttHeaderInfo header_info(int width) const;
    const core::gantt::GanttLayout& layout() const noexcept { return layout_; }

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    struct DayCell {
        core::sched::date32 date = core::sched::kDateNa;
        double x = 0.0;
        double width = 0.0;
        int weekday = -1;
    };
    struct WeekCell {
        // The date the tier prints: the Monday of the Sunday-first week (the
        // reference's "12 Oct '26" over an S-starting day row), or the first
        // visible day of a partial first week.
        core::sched::date32 label_date = core::sched::kDateNa;
        double x = 0.0;
        double width = 0.0;
    };

    std::vector<DayCell> visible_days(double x0, double x1) const;
    std::vector<WeekCell> visible_weeks(double x0, double x1) const;
    void paint_header(QPainter& painter, const std::vector<DayCell>& days,
                      const std::vector<WeekCell>& weeks, int canvas_width) const;

    core::gantt::GanttLayout layout_;
    std::uint32_t selected_uid_ = 0;
    bool shading_ = true;
};

}  // namespace msqt
