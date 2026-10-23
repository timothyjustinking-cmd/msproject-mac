// app_qt/backstage.h -- the File backstage shell (operator decision 2).
//
// The left rail is the product's order: Home, New, Open, Info, Save, Save As,
// Print, Share, Export, Close, with Options pinned at the bottom.  Working pages
// are real: New/Open/Save/Save As/Print/Export/Close call the same MainWindow
// commands as the menu, and Options carries the Office theme choice.  Info and
// Share carry honest local content only -- no Microsoft identity, no OneDrive,
// no fake cloud UI.
//
// No Q_OBJECT: buttons and combos use lambdas over std::function handlers.
#pragma once

#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

#include "theme.h"

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace msqt {

class Backstage final : public QWidget {
  public:
    explicit Backstage(QWidget* parent = nullptr);

    // The page indices, in nav order.  Options is pinned at the bottom.
    enum class Page : int {
        home = 0,
        create_new,
        open,
        info,
        save,
        save_as,
        print,
        share,
        export_file,
        close_file,
        options,
        count
    };

    // A command a page button asks the shell to run.
    void set_command_handler(std::function<void(const QString& command)> handler);
    void set_close_handler(std::function<void()> handler);
    void set_options_handler(std::function<void()> handler);
    void set_theme_handler(std::function<void(Theme)> handler);

    // Honest local project description for the Info page.
    void set_info_text(const QString& title, const QString& detail);

    QStackedWidget* pages() const noexcept { return pages_; }
    const std::vector<QToolButton*>& nav_buttons() const noexcept { return nav_buttons_; }
    // Wave 21 (operator defect): the circular back arrow at the very top-left,
    // above Home and left of the rail, exactly where Project 2021 puts it
    // (corpus/reference/2026-10-23-msproject/project-backstage-account.png).  It
    // calls the same close handler the Close page uses, so there is always a way
    // back to the project chrome (Escape does the same).
    QToolButton* back_button() const noexcept { return back_button_; }
    QComboBox* theme_combo() const noexcept { return theme_combo_; }
    QPushButton* options_button() const noexcept { return options_button_; }
    int nav_count() const;

    void show_page(Page page);
    void show_page(int index);

  private:
    QWidget* build_page(Page page, const QString& title, const QString& body,
                        const QString& command_label, const QString& command);

    QWidget* nav_rail_ = nullptr;
    QToolButton* back_button_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    std::vector<QToolButton*> nav_buttons_;
    std::vector<QWidget*> page_widgets_;
    QLabel* info_title_ = nullptr;
    QLabel* info_detail_ = nullptr;
    QComboBox* theme_combo_ = nullptr;
    QPushButton* options_button_ = nullptr;
    std::function<void(const QString&)> command_handler_;
    std::function<void()> close_handler_;
    std::function<void()> options_handler_;
    std::function<void(Theme)> theme_handler_;
};

}  // namespace msqt
