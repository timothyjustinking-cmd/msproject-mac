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
    void set_close_handler(std::function<void()> handler);
    void set_options_handler(std::function<void()> handler);
    void set_theme_handler(std::function<void(Theme)> handler);

    // Honest local project description for the Info page.
    void set_info_text(const QString& title, const QString& detail);

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
    QWidget* build_account_page();
    void set_nav_checked(int page_index);

    QWidget* nav_rail_ = nullptr;
    QToolButton* back_button_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    std::vector<QAbstractButton*> nav_buttons_;
    std::vector<QWidget*> page_widgets_;
    QLabel* info_title_ = nullptr;
    QLabel* info_detail_ = nullptr;
    QComboBox* theme_combo_ = nullptr;
    QAbstractButton* options_button_ = nullptr;
    std::function<void(const QString&)> command_handler_;
    std::function<void()> close_handler_;
    std::function<void()> options_handler_;
    std::function<void(Theme)> theme_handler_;
    bool has_document_ = false;
};

}  // namespace msqt
