// app_qt/ribbon_bar.h -- the ribbon-like toolbar: tabs, groups, labelled
// buttons, at the heights the reference screenshot uses.
//
// Pixel-exact ribbon artwork is out of scope (wave 17 brief): this is structure
// and order, text buttons only, simple glyphs.  The tab set is Task, Resource,
// Report, Project, View, Help plus the contextual "Gantt Chart Format"; each tab
// owns ordered groups with a caption under the buttons, exactly as the ribbon
// lays them out.  Heights come from shell_style.h.
//
// WAVE 18 Q3: every button gets a FIXED width from its own label's QFontMetrics
// plus padding, and a group is as wide as the widest of its button row and its
// caption.  A narrow window therefore drops whole groups (in ribbon order) at
// the page's right edge instead of squeezing them: Qt's QToolButton clips text
// from BOTH ends when it is too narrow ("Cu", "t F"), which is the visible bug
// this replaces.  No label is ever cut, at 1920 or at 900.
//
// No Q_OBJECT: the widget has no custom signals; button clicks go through a
// std::function set by MainWindow.
#pragma once

#include <QSize>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

#include "menu_model.h"

class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class QToolButton;

namespace msqt {

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
    int button_count() const;
    // How many of the active tab's groups are actually on screen: a narrow
    // window drops whole groups rather than clipping their labels.
    int visible_group_count() const;

    // Buttons with a modelled command call the handler; the rest report through
    // the same handler with MenuAction::none (MainWindow shows "not wired").
    void set_action_handler(std::function<void(app::MenuAction)> handler);

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
    // One group's two widgets: the box (buttons + caption) and the separator
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
    std::function<void(app::MenuAction)> handler_;
    std::function<void()> file_handler_;
    int button_count_ = 0;
};

}  // namespace msqt
