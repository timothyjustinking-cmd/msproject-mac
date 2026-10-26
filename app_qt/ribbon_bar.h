// app_qt/ribbon_bar.h -- the ribbon-like toolbar: tabs, groups and the
// controls Project actually shows (large buttons, small stacked rows, combo
// boxes, check boxes, split buttons, a date field, the Gantt Chart Style
// gallery and dialog launchers).
//
// WAVE 26 (W26-1.1..1.5): the wave-22 model was text buttons only -- every
// icon-only control, combo, check box and the gallery was listed in
// ribbon_pin_unmodelled.tsv as backlog.  This header now models each of those
// kinds, with the live widget class as the source of truth for the kind (W25-7:
// the inventory gate compares the pin's kind column with the LIVE widget, never
// with the port's own spec).  The measured group rectangles come from the clean
// ribbon strips; `tests/data/reference/w26-ribbon.measures.json` carries the
// numbers, and the gate compares the live geometry with them.
//
// The controls are laid out in Project's grid: three 22-px stack rows at a
// measured 5-px offset from the top of the group band, a 64-px large button
// spanning all three, and the group caption at the bottom.  A group's width is
// FIXED to the measured clean strip, so the group rectangles do not drift with
// the font metrics; the page drops whole groups at the right edge exactly as
// before.
//
// No Q_OBJECT: the widget has no custom signals; clicks and toggles go through
// std::function handlers set by MainWindow.
#pragma once

#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <vector>

#include "menu_model.h"
#include "theme.h"

class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class QToolButton;
class QFrame;

namespace msqt {

// Wave 26: ribbon controls that are not app::MenuActions.  They are commands
// the ribbon offers that the menu bar does not: the %-complete quick buttons,
// the Link/Unlink task buttons, and the format/view toggles.  The shell handles
// them through set_command_handler(); their live wiring is pinned by the
// inventory gate.
enum class RibbonCommand : std::uint8_t {
    none = 0,
    percent_0,
    percent_25,
    percent_50,
    percent_75,
    percent_100,
    link_tasks,
    unlink_tasks,
    outdent,
    indent,
    // Wave 30 (W30-1.0): the Task tab's Manually Schedule button and the Mode
    // button's menu, both on the measured automatic -> manual task-mode writer
    // (app::make_task_mode_edit -> mppwrite::write_task_mode).  Auto Schedule
    // stays a plain disabled control (decision 9 tooltip): the reverse pair
    // shows Project also reschedules the row and this port has no scheduler.
    manually_schedule,
    task_mode,
    split_task,
    toggle_critical_tasks,
    toggle_project_summary,
    toggle_timeline,
    toggle_details,
    // Wave 30 (W30-1.5): the Task tab's Details button.  Project's button
    // opens the Task Form details pane, which this port already docks through
    // View > Details (operator decision 16).  The Task-tab button SHOWS the
    // pane (the View check box remains the toggle), so a click can never hide
    // a pane the user is reading; a view command, never a file write.
    task_details,
    // Wave 30 (W30-1.7): the Task tab's Scroll to Task.  It scrolls the sheet
    // to the selected row and pans the chart to the task's start; a view
    // command, never a file write.
    scroll_to_task,
    // Wave 30 (W30-1.8): the Task tab's Fill Down.  W31-0.4 (R30-4) DISABLED
    // it: Project's measured Fill Down save rewrites the edited row's
    // calculated dates, the project roll-up and the DSI summary strings, while
    // the port's start writer writes only the constraint window.  The control
    // keeps this command for the disabled-control wiring and the guarded
    // handler, but no UI path reaches it.
    fill_down,
    // Wave 30 (W30-1.10): the Task tab's Inspect.  It toggles the read-only
    // Task Inspector pane (the selected row's scheduler facts, from the plan
    // by UID); a view command, never a file write.
    inspect,
    // Wave 30 (W30-3.3): the Project tab's Spelling.  It opens a read-only
    // report of the task names that contain a word outside the port's own
    // word list (app::spelling_issues).  No Microsoft Project dictionary data,
    // no correction and no write path: a view command.
    spelling,
    // Wave 30 (W30-2.2): the Resource tab's Details.  Project opens the
    // Resource Form; this port docks its read-only Resource Form in the
    // Details splitter and switches to the Resource Sheet so the pane has a
    // selection to follow.  A view command, never a file write.
    resource_details,
    // Wave 26 (G5): the Project tab's dialogs.  Custom Fields keeps its own
    // MenuAction (it is also on the Gantt Chart Format tab); these are ribbon
    // commands with no menu-bar item.
    project_information,
    change_working_time,
    links_between_projects,
    update_project,
    move_project,
    // Wave 28 (W28-1.3): Mark on Track's main button on the measured actuals
    // writer.  Its drop-down has no measured menu, so only the button runs.
    mark_on_track,
    // The two drop-down buttons: their click opens the menu the capture shows.
    wbs_menu,
    set_baseline_menu,
    // Wave 29 (W29-2.1): the Gantt Chart Format tab's three dialogs.
    text_styles,
    gridlines,
    bar_styles,
    // Wave 29 (W29-4.1): the Report tab's two dialogs.  Both are laid out from
    // the captures and inert (no report or comparison engine), so the buttons
    // are live because they open a real dialog that says so, not because the
    // dialog can change the project.
    compare_projects,
    visual_reports,
    // Wave 31 (W31-1.1): the View tab's Data group.  Sort opens the Sort
    // dialog; the order it applies is SESSION-ONLY (the RecordSet and the file
    // keep their order), because Project's own sort is a persisted reorder this
    // wave did not measure.  app/sort_model.h owns the outline-aware order.
    data_sort,
    // Wave 31 (W31-1.2): Outline opens Project's level menu (Level 1..9 / All
    // Subtasks).  The shown levels are SESSION-ONLY: the sheet's collapse state,
    // never the file's rows or bytes.
    data_outline,
    // Wave 31 (W31-1.3): Tables lists the task tables the file stores and
    // switches the sheet's columns for the session only.
    data_tables,
    // Wave 31 (W31-2.1): the View tab's Zoom group.  The dialog's result replaces
    // the chart's timescale for the session only (PlanDocument::zoom_fixed_span).
    zoom_dialog,
    // Wave 31 (W31-2.2): the two span commands, fitted to the same 85 % rule
    // Project uses and measured against its windows on both fixtures.
    zoom_entire,
    zoom_selected,
    // Wave 31 (W31-2.3): the View tab's Window group.  This port is a
    // single-document shell: there is no second window to open or list, one
    // window has nothing to arrange, and hiding the only window would leave no
    // UI.  Each control keeps its own command id for the disabled-control
    // wiring and a specific backlog reason, and stays DISABLED with the
    // decision-9 tooltip.
    window_new,
    window_switch,
    window_arrange,
    window_hide,
    // Wave 31 (W31-2.4): the View tab's Macros button.  There is no VBA engine
    // (no runtime, no editor, no macro storage), so the button stays DISABLED
    // with the decision-9 tooltip; the id exists for the disabled-control
    // wiring and the specific backlog reason.
    macros,
    // Wave 31 (W31-5.1): the View tab's Task Views / Resource Views entries for
    // the views this run does not build.  Network Diagram, Calendar and both
    // Other Views pickers are a SCOPE decision (the brief's G5 decides, it does
    // not build), so each control keeps a command id for the disabled-control
    // wiring and a specific backlog reason, and stays DISABLED with the
    // decision-9 tooltip.
    view_network_diagram,
    view_calendar,
    view_other_views,
};

class RibbonBar final : public QWidget {
  public:
    explicit RibbonBar(QWidget* parent = nullptr);
    ~RibbonBar() override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    int tab_count() const;
    QStringList tab_titles() const;
    // The groups currently shown (the active tab's), by their caption.
    QStringList group_titles() const;
    // Large command buttons only (the measured 64-px shape).  Small stacked
    // buttons, combos, check boxes, split buttons and the gallery are not part
    // of this count; the inventory gate reads them through group_control().
    int button_count() const;
    // How many of the active tab's groups are actually on screen: a narrow
    // window drops whole groups rather than clipping their labels.
    int visible_group_count() const;

    // The active tab can be selected without a mouse click so the gate can pin
    // every one of the seven tabs, and the live page can be read back group by
    // group in ribbon order (hidden groups included).
    int active_tab_index() const noexcept { return active_tab_; }
    void set_active_tab(int index);

    // --- WAVE 26: the live control inventory --------------------------------
    // One group's controls, in ribbon order (left to right, then top to
    // bottom).  The pointer is the LIVE widget: the gate classifies it through
    // control_kind() and reads its own text/enabled/checked state, so a control
    // built as the wrong class cannot pass.
    int group_control_count(int group_index) const;
    QWidget* group_control(int group_index, int control_index) const;
    // The group's own box (controls + caption).  The inventory gate measures the
    // live control rows against it.
    QWidget* group_box(int group_index) const;
    app::MenuAction group_control_action(int group_index, int control_index) const;
    RibbonCommand group_control_command(int group_index, int control_index) const;
    // W26-2: the pin's icon key for a command control (button/small/split), or
    // an empty string for a control that draws no command glyph.  The icon gate
    // compares this with the pin's icon column and hashes the LIVE widget's
    // rendered icon, so the key can never pass for artwork on its own.
    QString group_control_icon_key(int group_index, int control_index) const;
    // The spec's stack row (0..2; -1 for a control that is not a stack row,
    // such as the gallery or a dialog launcher).  The gate proves the live
    // geometry against the measured row baselines.
    int group_control_row(int group_index, int control_index) const;
    // The kind of a live control, read from the WIDGET (its class, or the
    // small QWidget/QToolButton subclasses this file builds) -- never from the
    // pin file.  Public and static so the inventory gate can prove the
    // classifier with real QWidgets.
    static QString control_kind(const QWidget* control);
    QString group_control_kind(int group_index, int control_index) const;

    // Buttons with a modelled MenuAction call the action handler; small buttons
    // and toggles call the command handler.  A disabled control never calls
    // either (Qt blocks the signal), and the gate invokes the live widget.
    void set_action_handler(std::function<void(app::MenuAction)> handler);
    void set_command_handler(std::function<void(RibbonCommand, bool)> handler);

    // Wave 26: the document state the ribbon's check boxes show.  The Timeline
    // and Project Summary Task boxes are file state (wave 24): their initial
    // state is the loaded file's stored setting, and the shell keeps the live
    // control in step through this call.  A new project uses Project's defaults
    // (Timeline shown, project row hidden).  The state survives a tab switch
    // because it is held here, not in the page widgets.
    void set_document_state(bool has_document, bool timeline_shown,
                            bool project_summary_shown);
    bool timeline_checked() const noexcept { return timeline_checked_; }
    bool project_summary_checked() const noexcept { return project_summary_checked_; }
    bool critical_tasks_checked() const noexcept { return critical_checked_; }
    // Flipping Critical Tasks is not document state; a new/loaded file resets
    // the measured default (OFF), and the live check box follows.
    void set_critical_tasks(bool checked) {
        critical_checked_ = checked;
        set_stack_check(QStringLiteral("Critical Tasks"), checked);
    }
    // W30-1.5: the View tab's Details box reflects the pane the Task tab's
    // Details button (or the box itself) opened.  Held here so it survives a
    // tab switch, like the two document check boxes.
    void set_details_checked(bool checked) {
        details_checked_ = checked;
        set_stack_check(QStringLiteral("Details"), checked);
    }

    // W26-REV-4: the Font group's two value displays.  The shell sets the
    // FILE's decoded default text style on load; an empty family / non-positive
    // size restores the captured new-project default (Calibri 11).  These are
    // displays, not a file edit.
    void set_font_default(const QString& family, int point_size);

    // W26-6.2: the chrome palette the ribbon paints and styles itself with.
    // The light theme is the measured parity chrome; Dark Gray and Black swap
    // the tab row, the group-row surface, the separators and every label
    // colour.  Reapplies the stylesheet and repaints in place (live).
    void set_theme(Theme theme);

    // The File tab is chrome, not one of the seven modelled command tabs: it has
    // its own styling and opens the backstage through this handler.
    void set_file_handler(std::function<void()> handler);
    QToolButton* file_tab() const noexcept { return file_tab_; }
    QLabel* tell_me_label() const noexcept { return tell_me_; }
    QString tell_me_text() const;

    // The contextual "Gantt Chart Format" tab exists only in the Gantt view
    // (operator decision 4); the grid views hide it.
    void set_contextual_visible(bool visible);
    QToolButton* contextual_tab() const noexcept { return contextual_tab_; }

    // Wave 20 overflow chrome: a narrow window shows one ">>" chevron at the
    // group row's right edge instead of wrapping, and clips the tab strip behind
    // a tab chevron.  Both are real buttons the structural gate can find.
    QToolButton* overflow_button() const noexcept { return overflow_; }
    QToolButton* tab_overflow_button() const noexcept { return tab_overflow_; }
    bool groups_overflow() const noexcept { return groups_overflow_; }

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    // One group's two widgets: the box (controls + caption) and the separator
    // after it.  They are hidden together when the group does not fit.
    struct GroupWidgets {
        QWidget* box = nullptr;
        QWidget* separator = nullptr;
    };

    void build_tabs();
    void build_page(int tab_index);
    void clear_page();
    // W26-6.2: applies the current theme's stylesheet to the produced children
    // (tabs, buttons, combos, check boxes, separators) and repaints.
    void apply_theme_style();
    // W29-5.1: re-renders the active page's command glyphs in the theme's
    // one-ink colour (ChromePalette::ribbon_ink), so Black's light ink lands
    // without a page rebuild.
    void apply_icon_ink();
    // Shows whole groups from the left until the next one would pass the right
    // edge; the rest are hidden (never half-drawn, so no label is cut).
    void layout_page_groups();
    void update_tab_overflow();
    void sync_document_checkboxes();
    void set_stack_check(const QString& accessible_name, bool checked);

    QWidget* tab_row_ = nullptr;
    QWidget* page_ = nullptr;
    QHBoxLayout* page_layout_ = nullptr;
    QFrame* rule_ = nullptr;
    QToolButton* file_tab_ = nullptr;
    QToolButton* contextual_tab_ = nullptr;
    QToolButton* overflow_ = nullptr;
    QToolButton* tab_overflow_ = nullptr;
    std::vector<QToolButton*> tab_buttons_;
    bool groups_overflow_ = false;
    bool contextual_enabled_ = true;
    QLabel* tell_me_ = nullptr;
    std::vector<GroupWidgets> page_groups_;
    // WAVE 26: per group of the active page, its live controls and their
    // modelled command/action/stack row, in ribbon order.  Same shape/order as
    // page_groups_.
    std::vector<std::vector<QWidget*>> page_controls_;
    std::vector<std::vector<app::MenuAction>> page_actions_;
    std::vector<std::vector<RibbonCommand>> page_commands_;
    std::vector<std::vector<QString>> page_icons_;
    std::vector<std::vector<int>> page_rows_;
    std::function<void(app::MenuAction)> handler_;
    std::function<void(RibbonCommand, bool)> command_handler_;
    std::function<void()> file_handler_;
    int active_tab_ = 0;
    int button_count_ = 0;
    // W26-6.2: the resolved palette this ribbon paints with.
    Theme theme_ = Theme::light;

    // Wave 26: the document-driven check-box state (see set_document_state()).
    bool has_document_ = false;
    bool timeline_checked_ = true;
    bool project_summary_checked_ = false;
    // Critical Tasks is a view toggle whose measured default is OFF.
    bool critical_checked_ = false;
    // W30-1.5: the Details pane's shown state (the View box and the Task tab's
    // Details button share it).
    bool details_checked_ = false;
    // W26-REV-4: the Font group's value displays (the file's decoded default,
    // else Project's captured new-project default).
    QString font_family_ = QStringLiteral("Calibri");
    QString font_size_ = QStringLiteral("11");
};

}  // namespace msqt
