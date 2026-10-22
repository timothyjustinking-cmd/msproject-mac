// app_qt/ribbon_bar.h -- the ribbon-like toolbar: tabs, groups, labelled
// buttons, at the heights the reference screenshot uses.
//
// Pixel-exact ribbon artwork is out of scope (wave 17 brief): this is structure
// and order, text buttons only, simple glyphs.  The tab set is Task, Resource,
// Report, Project, View, Help plus the contextual "Gantt Chart Format"; each tab
// owns ordered groups with a caption under the buttons, exactly as the ribbon
// lays them out.  Heights come from shell_style.h.
//
// No Q_OBJECT: the widget has no custom signals; button clicks go through a
// std::function set by MainWindow.
#pragma once

#include <QSize>
#include <QStringList>
#include <QWidget>

#include <functional>

#include "menu_model.h"

class QHBoxLayout;
class QPaintEvent;

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

    // Buttons with a modelled command call the handler; the rest report through
    // the same handler with MenuAction::none (MainWindow shows "not wired").
    void set_action_handler(std::function<void(app::MenuAction)> handler);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    void build_tabs();
    void build_page(int tab_index);
    void clear_page();

    QWidget* tab_row_ = nullptr;
    QWidget* page_ = nullptr;
    QHBoxLayout* page_layout_ = nullptr;
    std::function<void(app::MenuAction)> handler_;
    int active_tab_ = 0;
    int button_count_ = 0;
};

}  // namespace msqt
