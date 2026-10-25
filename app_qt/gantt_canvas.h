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

#include <QColor>
#include <QSize>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

#include "gantt.h"
#include "gantt_style.h"
#include "theme.h"  // Theme (W29-5.1: the timescale band follows it)
#include "view_style.h"

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
    // The current preference, for the preferences gate and the status line.
    bool shading() const noexcept { return shading_; }
    // Wave 25 (W25-2/S2): Format > Critical Tasks highlighting.  Project's
    // default is OFF, so ordinary painting uses the auto/manual bar styles;
    // ON paints critical tasks and links with the measured highlight colours.
    // The ribbon action is still unimplemented, so the shell passes nothing.
    void set_critical_highlight(bool enabled);
    bool critical_highlight() const noexcept { return critical_highlight_; }

    // Wave 26 (W26-3.2): the loaded file's Gantt style record.  When it
    // decodes, the bars use the FILE's bar-style colours and the completion
    // fill uses its Progress/Finished style; otherwise the wave-25 measured
    // stock palette stands.  The pointer must outlive the canvas.
    void set_gantt_style(const msproject::viewstate::GanttStyle* style);
    bool uses_file_style() const noexcept { return palette_.decoded; }

    // W29-5.1: the timescale band is chrome -- Dark Gray and Black paint the
    // measured header fills and rules (theme.h).  The chart BODY below the band
    // keeps the file's colours, so only the band repaints (decision 10).
    void set_theme(Theme theme);
    Theme theme() const noexcept { return theme_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Geometry of the two-tier header for a widget of `width` pixels.
    GanttHeaderInfo header_info(int width) const;
    const core::gantt::GanttLayout& layout() const noexcept { return layout_; }

    // Wave 23 (A-1): the fill colour paintEvent gives layout bar `index` --
    // auto/manual summary, milestone, highlighted-critical, manual or ordinary
    // task.  Exposed so the same-file
    // structural gate can compare the per-row table against literals instead
    // of re-reading the code it is testing.
    QColor bar_colour(std::size_t index) const;

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
    // Wave 26 (W26-3.3): one cell of a timescale tier and the tier itself.
    // The tier list comes from the FILE's stored units/formats when the style
    // record decodes, else from the measured stock week/day pair.
    struct TierCell {
        double x = 0.0;
        double width = 0.0;
        std::string label;
        int weekday = -1;  // day tier only
    };
    struct HeaderTier {
        msproject::viewstate::TimescaleTier spec;
        bool has_text_style = false;
        msproject::viewstate::TextStyle text;
        std::vector<TierCell> cells;
    };

    std::vector<DayCell> visible_days(double x0, double x1) const;
    std::vector<WeekCell> visible_weeks(double x0, double x1) const;
    // Wave 26 (W26-3.3): the tiers Project stores for this view (a stock file
    // stores week/day with no top tier; PROJECT_A stores year/quarter/month).
    std::vector<HeaderTier> header_tiers(const std::vector<DayCell>& days,
                                        const std::vector<WeekCell>& weeks) const;
    void paint_header(QPainter& painter, const std::vector<DayCell>& days,
                      const std::vector<WeekCell>& weeks, int canvas_width) const;
    // Wave 26 (W26-3.2): the fill colour for a bar, file palette first.
    QColor bar_colour_for(const core::gantt::Bar& bar) const;

    core::gantt::GanttLayout layout_;
    std::uint32_t selected_uid_ = 0;
    bool shading_ = true;
    bool critical_highlight_ = false;  // Project's default: Critical Tasks OFF
    const msproject::viewstate::GanttStyle* style_ = nullptr;
    app::BarPalette palette_;  // cached from style_ by set_gantt_style()
    Theme theme_ = Theme::light;  // W29-5.1: the timescale band's palette
};

}  // namespace msqt
