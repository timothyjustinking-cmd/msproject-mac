// app_qt/backstage.h -- the File backstage shell (operator decision 2, wave 26).
//
// WAVE 26 (W26-4.1): the rail is Project 2021's, measured from the clean
// `project-backstage-account.png` capture (1903x1033, local only):
//
//   * a 133-px light rail (#F0F0F0) on the content's #F5F5F5;
//   * the circular back arrow 21x21 at (10, 14) below the shell's top;
//   * Home / New / Open with monochrome glyphs, then a separator and the
//     text-only Info / Save / Save As / Print / Share / Export / Close;
//   * Account under Close, above the pinned Options entry, with the measured
//     40-px entry pitch and a #D1D1D1 separator;
//   * disabled entries use the capture's #999999 (Save..Close are disabled
//     while no document is open, as the clean Account capture shows).
//
// The File tab itself is a PLAIN ribbon tab in Project (audit finding B-7); the
// dark tile wave 20 drew is gone (ribbon_bar.cpp owns that stylesheet).
//
// Pages are real: New/Open/Save/Save As/Print/Export/Close call the same
// MainWindow commands as the menu, Options opens the Options dialog, and Info
// and Account carry honest local content only -- no Microsoft identity, no
// OneDrive, no fake cloud UI.
//
// No Q_OBJECT: buttons and combos use lambdas over std::function handlers.
#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

#include "theme.h"

class QAbstractButton;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace msqt {

class Backstage final : public QWidget {
  public:
    explicit Backstage(QWidget* parent = nullptr);

    // The page indices, in nav order.  Account is the last page; Options is an
    // ACTION at the bottom of the rail (it opens the Options dialog), not a
    // page (W26-4.6).
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
        account,
        count
    };

    // A command a page button asks the shell to run.
    void set_command_handler(std::function<void(const QString& command)> handler);
    // The installed handler (the gate swaps in a recorder to prove a button's
    // command without opening the modal file dialog).
    const std::function<void(const QString&)>& command_handler() const noexcept {
        return command_handler_;
    }
    void set_close_handler(std::function<void()> handler);
    void set_options_handler(std::function<void()> handler);
    // W26-4.2: the Account page's Office Theme chooser.  Only an implemented
    // choice calls the handler (Black/White are laid out and disabled).
    void set_theme_choice_handler(std::function<void(ThemeChoice)> handler);
    void set_about_handler(std::function<void()> handler);

    // Honest local project description for the Info page.
    void set_info_text(const QString& title, const QString& detail);
    // The port's own identity, version and local note for the Account page.
    void set_account_info(const QString& product, const QString& version,
                          const QString& detail);
    // Selects the chooser's current theme without emitting the handler.
    void set_theme_choice(ThemeChoice choice);

    // W26-4.3: the local recent-file list (New page; Open/Save As add their own
    // views in W26-4.4).  The caller supplies local paths only.
    void set_recent_files(const QStringList& files);
    void set_recent_open_handler(std::function<void(const QString&)> handler);
    // W26-4.3 accessors for the structural gate: the live New page controls.
    QAbstractButton* new_blank_button() const noexcept { return new_blank_; }
    QAbstractButton* new_from_existing_button() const noexcept { return new_existing_; }
    QLineEdit* online_search() const noexcept { return online_search_; }
    QLabel* online_note() const noexcept { return online_note_; }
    QListWidget* new_recent_list() const noexcept { return new_recent_; }

    // W26-4.1: the rail's command entries follow the document state.  With no
    // document open the clean Account capture shows Save..Close disabled.
    void set_document_state(bool has_document);

    QStackedWidget* pages() const noexcept { return pages_; }
    // The rail's buttons in order: the eleven page entries (Home..Close,
    // Account) followed by the Options action (index 11).  They are our
    // measured RailButton painters (a plain QToolButton centres its text, which
    // would put Info at x=74 instead of the capture's 38).
    const std::vector<QAbstractButton*>& nav_buttons() const noexcept {
        return nav_buttons_;
    }
    // Wave 21 (operator defect): the circular back arrow at the very top-left,
    // above Home and left of the rail, exactly where Project 2021 puts it
    // (corpus/reference/2026-10-23-msproject/project-backstage-account.png).  It
    // calls the same close handler the Close page uses, so there is always a way
    // back to the project chrome (Escape does the same).
    QToolButton* back_button() const noexcept { return back_button_; }
    QAbstractButton* options_button() const noexcept { return options_button_; }
    // W26-4.2: the Account page's live controls, for the structural gate.
    QComboBox* theme_combo() const noexcept { return theme_combo_; }
    QAbstractButton* about_button() const noexcept { return about_button_; }
    QLabel* product_label() const noexcept { return product_label_; }
    QLabel* version_label() const noexcept { return version_label_; }
    QWidget* account_left_column() const noexcept { return account_left_column_; }
    QWidget* account_right_column() const noexcept { return account_right_column_; }
    int nav_count() const;

    void show_page(Page page);
    void show_page(int index);

    // The shell is a page of the central stack, so its minimum size must not
    // force the window above the pinned 900x560 test size (wave 20/26 render
    // tests).  At 560 px the rail's last pixels may clip; the window contract is
    // the pinned size, and Project's own rail behaves the same on a short
    // window.
    QSize minimumSizeHint() const override;

    // W26-4.1 measurements, from the clean Account capture.  The gate compares
    // the LIVE geometry with these rather than the port's constants.
    static constexpr int kRailWidth = 133;
    static constexpr int kNavEntryHeight = 40;
    static constexpr int kNavTextLeft = 38;
    static constexpr int kBackArrowSize = 21;
    static constexpr int kBackArrowLeft = 10;
    static constexpr int kBackArrowTop = 14;

  private:
    QWidget* build_page(Page page, const QString& title, const QString& body,
                        const QString& command_label, const QString& command);
    QWidget* build_new_page();
    QWidget* build_location_page(Page page);
    QWidget* build_account_page();
    void set_nav_checked(int page_index);
    void rebuild_recent_lists();

    QWidget* nav_rail_ = nullptr;
    QToolButton* back_button_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    std::vector<QAbstractButton*> nav_buttons_;
    std::vector<QWidget*> page_widgets_;
    QLabel* info_title_ = nullptr;
    QLabel* info_detail_ = nullptr;
    QComboBox* theme_combo_ = nullptr;
    QAbstractButton* options_button_ = nullptr;
    QAbstractButton* about_button_ = nullptr;
    QLabel* product_label_ = nullptr;
    QLabel* version_label_ = nullptr;
    QLabel* account_detail_ = nullptr;
    QWidget* account_left_column_ = nullptr;
    QWidget* account_right_column_ = nullptr;
    std::function<void(const QString&)> command_handler_;
    std::function<void()> close_handler_;
    std::function<void()> options_handler_;
    std::function<void(ThemeChoice)> theme_choice_handler_;
    std::function<void()> about_handler_;
    std::function<void(const QString&)> recent_open_handler_;
    QStringList recent_files_;
    QAbstractButton* new_blank_ = nullptr;
    QAbstractButton* new_existing_ = nullptr;
    QLineEdit* online_search_ = nullptr;
    QLabel* online_note_ = nullptr;
    QListWidget* new_recent_ = nullptr;
    QListWidget* open_recent_ = nullptr;
    QListWidget* save_as_recent_ = nullptr;
    bool has_document_ = false;
};

}  // namespace msqt
