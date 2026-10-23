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

class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class QToolButton;

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
    split_task,
    toggle_critical_tasks,
    toggle_project_summary,
    toggle_timeline,
    toggle_details,
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
    // Shows whole groups from the left until the next one would pass the right
    // edge; the rest are hidden (never half-drawn, so no label is cut).
    void layout_page_groups();
    void update_tab_overflow();
    void sync_document_checkboxes();
    void set_stack_check(const QString& accessible_name, bool checked);

    QWidget* tab_row_ = nullptr;
    QWidget* page_ = nullptr;
    QHBoxLayout* page_layout_ = nullptr;
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

    // Wave 26: the document-driven check-box state (see set_document_state()).
    bool has_document_ = false;
    bool timeline_checked_ = true;
    bool project_summary_checked_ = false;
    // Critical Tasks is a view toggle whose measured default is OFF.
    bool critical_checked_ = false;
};

}  // namespace msqt
