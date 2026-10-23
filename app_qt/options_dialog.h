// app_qt/options_dialog.h -- Project 2021's File > Options dialog (wave 26 G4).
//
// The eleven pages and their controls are laid out from the local audit
// captures (corpus/reference/2026-10-23-audit/, all git-ignored):
//   General, Display, Schedule, Proofing, Save, Language, Advanced,
//   Customize Ribbon, Quick Access Toolbar, Add-ins, Trust Center.
// Every control visible in the capture exists in its page's own order.  The two
// functional surfaces are deliberate and measured:
//   * General: the Date format combo (the port's three stored forms) and the
//     Office Theme combo (the Account page's five names; implemented ones
//     enabled);
//   * Schedule: "New tasks created" (Auto Scheduled / Manually Scheduled),
//     which changes the shell's live new-task mode.
// Everything else is laid out and disabled with decision 9's exact tooltip
// "not implemented in this port" -- the port has no Project Web App, cache,
// add-in, proofing or ribbon-customisation engine.
//
// The dialog is a plain QDialog with no Q_OBJECT; the gate constructs it
// directly and reads the live widget properties.
#pragma once

#include <QDialog>
#include <QString>

#include <vector>

#include "prefs.h"
#include "theme.h"

class QAbstractButton;
class QComboBox;
class QListWidget;
class QStackedWidget;
class QWidget;

namespace msqt {

class OptionsDialog final : public QDialog {
  public:
    OptionsDialog(const app::Preferences& current, ThemeChoice theme_choice,
                  bool new_tasks_manual, const QString& project_name,
                  QWidget* parent = nullptr);

    // The values OK would apply.  Valid at any time (Cancel discards them).
    const app::Preferences& preferences() const noexcept { return preferences_; }
    ThemeChoice theme_choice() const;
    bool new_tasks_manual() const;

    // The live page structure, for the structural gate.
    int page_count() const;
    QString page_title(int index) const;
    QWidget* page_widget(int index) const;
    QListWidget* page_list() const noexcept { return page_list_; }
    QComboBox* date_format_combo() const noexcept { return date_format_; }
    QComboBox* theme_combo() const noexcept { return theme_combo_; }
    QComboBox* new_tasks_combo() const noexcept { return new_tasks_; }

  private:
    QWidget* make_page(int index, const QString& title, const QString& subtitle);
    QWidget* make_scroll_page(QWidget* content);
    // The two functional pages.
    void build_general(QWidget* content);
    void build_schedule(QWidget* content);
    // The laid-out-only pages.
    void build_display(QWidget* content);
    void build_proofing(QWidget* content);
    void build_save(QWidget* content);
    void build_language(QWidget* content);
    void build_advanced(QWidget* content);
    void build_customize_ribbon(QWidget* content);
    void build_qat(QWidget* content);
    void build_addins(QWidget* content);
    void build_trust_center(QWidget* content);

    app::Preferences preferences_;
    ThemeChoice theme_choice_ = ThemeChoice::colorful;
    bool new_tasks_manual_ = true;
    QString project_name_;

    QListWidget* page_list_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    std::vector<QWidget*> page_contents_;
    QComboBox* date_format_ = nullptr;
    QComboBox* theme_combo_ = nullptr;
    QComboBox* new_tasks_ = nullptr;
};

}  // namespace msqt
