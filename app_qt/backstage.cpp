// app_qt/backstage.cpp -- see backstage.h.

#include "backstage.h"

#include <QComboBox>
#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <utility>

#include "shell_style.h"

namespace msqt {
namespace {

const QColor kContentBackground(0xff, 0xff, 0xff);

QFont backstage_font(int point_size, bool bold = false) {
    QFont font;
    font.setPointSize(point_size);
    font.setBold(bold);
    return font;
}

QLabel* make_body(QWidget* parent, const QString& text) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setFont(backstage_font(kBodyPointSize));
    label->setStyleSheet(QStringLiteral("color: #4a4a4a;"));
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return label;
}

}  // namespace

Backstage::Backstage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("backstage"));
    setAutoFillBackground(true);
    QPalette palette = QWidget::palette();
    palette.setColor(QPalette::Window, kContentBackground);
    setPalette(palette);

    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // --- left rail ---------------------------------------------------------
    nav_rail_ = new QWidget(this);
    nav_rail_->setObjectName(QStringLiteral("backstageRail"));
    nav_rail_->setFixedWidth(220);
    nav_rail_->setStyleSheet(QStringLiteral(
        "QWidget#backstageRail { background: #2b578a; }"
        "QToolButton#backstageNav { color: #ffffff; border: none; text-align: left;"
        " padding: 7px 18px; background: transparent; }"
        "QToolButton#backstageNav:hover { background: #3a6aa3; }"
        "QToolButton#backstageNav:checked { background: #ffffff; color: #2b578a; }"
        "QToolButton#backstageOptions { color: #ffffff; border: none; text-align: left;"
        " padding: 8px 18px; background: transparent; border-top: 1px solid #3a6aa3; }"
        "QToolButton#backstageOptions:hover { background: #3a6aa3; }"));
    auto* rail_layout = new QVBoxLayout(nav_rail_);
    rail_layout->setContentsMargins(0, 10, 0, 0);
    rail_layout->setSpacing(0);

    // Wave 21: the circular BACK arrow at the very top of the rail, above Home --
    // Project 2021's position.  Before this the backstage had no way back except
    // the Close nav entry; the button calls the same close handler as Escape.
    back_button_ = new QToolButton(nav_rail_);
    back_button_->setObjectName(QStringLiteral("backstageBack"));
    back_button_->setText(QString::fromUtf8("\xE2\x86\x90"));  // U+2190 leftwards arrow
    back_button_->setToolTip(QStringLiteral("Back (Esc)"));
    back_button_->setAccessibleName(QStringLiteral("Back"));
    back_button_->setCursor(Qt::PointingHandCursor);
    back_button_->setFixedSize(28, 28);
    back_button_->setFont(backstage_font(12, true));
    back_button_->setStyleSheet(QStringLiteral(
        "QToolButton#backstageBack { color: #ffffff; background: #2b578a;"
        " border: 1px solid #ffffff; border-radius: 14px; }"
        "QToolButton#backstageBack:hover { background: #3a6aa3; }"));
    connect(back_button_, &QToolButton::clicked, this, [this](bool) {
        if (close_handler_) {
            close_handler_();
        }
    });
    auto* back_row = new QHBoxLayout();
    back_row->setContentsMargins(14, 0, 0, 8);
    back_row->setSpacing(0);
    back_row->addWidget(back_button_);
    back_row->addStretch(1);
    rail_layout->addLayout(back_row);

    struct NavSpec {
        const char* label;
        Page page;
    };
    const NavSpec nav_specs[] = {
        {"Home", Page::home},       {"New", Page::create_new},
        {"Open", Page::open},       {"Info", Page::info},
        {"Save", Page::save},       {"Save As", Page::save_as},
        {"Print", Page::print},     {"Share", Page::share},
        {"Export", Page::export_file}, {"Close", Page::close_file},
    };
    for (const NavSpec& spec : nav_specs) {
        auto* button = new QToolButton(nav_rail_);
        button->setObjectName(QStringLiteral("backstageNav"));
        button->setText(QString::fromUtf8(spec.label));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFont(backstage_font(kBodyPointSize));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setFixedHeight(34);
        const int index = static_cast<int>(spec.page);
        connect(button, &QToolButton::clicked, this,
                [this, index](bool) { show_page(index); });
        rail_layout->addWidget(button);
        nav_buttons_.push_back(button);
    }
    rail_layout->addStretch(1);

    // Options is pinned at the bottom of the rail (operator decision 2).
    auto* options = new QToolButton(nav_rail_);
    options->setObjectName(QStringLiteral("backstageOptions"));
    options->setText(QStringLiteral("Options"));
    options->setCheckable(true);
    options->setAutoExclusive(true);
    options->setToolButtonStyle(Qt::ToolButtonTextOnly);
    options->setFont(backstage_font(kBodyPointSize));
    options->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    options->setFixedHeight(36);
    connect(options, &QToolButton::clicked, this,
            [this](bool) { show_page(Page::options); });
    rail_layout->addWidget(options);
    nav_buttons_.push_back(options);
    outer->addWidget(nav_rail_);

    // --- pages -------------------------------------------------------------
    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("backstagePages"));
    pages_->setStyleSheet(QStringLiteral("QStackedWidget { background: #ffffff; }"));
    outer->addWidget(pages_, 1);

    struct PageSpec {
        Page page;
        const char* title;
        const char* body;
        const char* command_label;
        const char* command;
    };
    const PageSpec page_specs[] = {
        {Page::home, "Home",
         "This is a local project file viewer and writer. Open a project, or start "
         "from today's date. Nothing here is synced to a cloud service.",
         "Open a project...", "open"},
        {Page::create_new, "New",
         "Create a new project that opens on today's date, in Manually Scheduled "
         "mode, with the Entry table and an empty first row.",
         "Create a new project", "new"},
        {Page::open, "Open",
         "Open an .mpp file from this computer. The file is read through the shared "
         "portable reader; it is never modified in place.",
         "Browse for a project...", "open"},
        {Page::save, "Save",
         "Save the pending edits through the shared writer. A new target path is "
         "asked for when the file is not one this app wrote.",
         "Save the pending edits", "save"},
        {Page::save_as, "Save As",
         "Write the project to a new .mpp file, leaving the opened file untouched.",
         "Choose a target...", "save_as"},
        {Page::print, "Print",
         "Print the task sheet and Gantt through the shared page layout.",
         "Print the project", "print"},
        {Page::share, "Share",
         "This port has no cloud sharing, OneDrive or Microsoft account integration. "
         "Use Export to write a local PDF copy and share that file yourself.",
         "Export a local copy...", "export"},
        {Page::export_file, "Export",
         "Export the visible project as a PDF through the shared core renderer.",
         "Export PDF...", "export"},
        {Page::close_file, "Close",
         "Close this project window. Unsaved edits are confirmed first.",
         "Close the window", "close"},
    };
    // The pages are indexed by Page, so Info (3) and Options (10) are inserted
    // into their nav slots below rather than appended out of order.
    std::vector<QWidget*> ordered(static_cast<std::size_t>(Page::count), nullptr);
    for (const PageSpec& spec : page_specs) {
        QWidget* page = build_page(spec.page, QString::fromUtf8(spec.title),
                                   QString::fromUtf8(spec.body),
                                   QString::fromUtf8(spec.command_label),
                                   QString::fromUtf8(spec.command));
        ordered[static_cast<std::size_t>(spec.page)] = page;
        page_widgets_.push_back(page);
    }

    // Info: honest local project facts, refreshed by set_info_text().
    {
        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(36, 28, 36, 28);
        layout->setSpacing(10);
        auto* title = new QLabel(QStringLiteral("Info"), page);
        title->setFont(backstage_font(20, true));
        title->setStyleSheet(QStringLiteral("color: #202020;"));
        layout->addWidget(title);
        info_title_ = new QLabel(QStringLiteral("No project is open"), page);
        info_title_->setFont(backstage_font(kBodyPointSize, true));
        layout->addWidget(info_title_);
        info_detail_ = make_body(page, QStringLiteral("Open a project to see its details."));
        layout->addWidget(info_detail_);
        auto* note = make_body(
            page, QStringLiteral("This port has no Microsoft account, cloud location or "
                                 "remote project information. The facts above are read from "
                                 "the local file only."));
        layout->addWidget(note);
        layout->addStretch(1);
        ordered[static_cast<std::size_t>(Page::info)] = page;
        page_widgets_.push_back(page);
    }

    // Options: the Office theme choice (Options > General) + Preferences.
    {
        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(36, 28, 36, 28);
        layout->setSpacing(10);
        auto* title = new QLabel(QStringLiteral("Options"), page);
        title->setFont(backstage_font(20, true));
        title->setStyleSheet(QStringLiteral("color: #202020;"));
        layout->addWidget(title);
        auto* general = new QLabel(QStringLiteral("General"), page);
        general->setFont(backstage_font(kBodyPointSize, true));
        layout->addWidget(general);
        auto* row = new QHBoxLayout();
        row->setSpacing(10);
        row->addWidget(new QLabel(QStringLiteral("Office Theme"), page));
        theme_combo_ = new QComboBox(page);
        theme_combo_->setObjectName(QStringLiteral("officeThemeCombo"));
        theme_combo_->addItem(QStringLiteral("Office Light"));
        theme_combo_->addItem(QStringLiteral("Office Dark"));
        theme_combo_->setFixedWidth(220);
        row->addWidget(theme_combo_);
        row->addStretch(1);
        layout->addLayout(row);
        options_button_ = new QPushButton(QStringLiteral("All preferences..."), page);
        options_button_->setFixedHeight(28);
        options_button_->setMaximumWidth(200);
        layout->addWidget(options_button_);
        layout->addWidget(make_body(
            page, QStringLiteral("The light theme is the parity reference; the dark theme "
                                 "uses the Office dark values. More Options pages follow in "
                                 "the dialog cut.")));
        layout->addStretch(1);
        ordered[static_cast<std::size_t>(Page::options)] = page;
        page_widgets_.push_back(page);

        connect(theme_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (theme_handler_) {
                theme_handler_(index == 1 ? Theme::dark : Theme::light);
            }
        });
        connect(options_button_, &QPushButton::clicked, this, [this](bool) {
            if (options_handler_) {
                options_handler_();
            }
        });
    }

    for (QWidget* page : ordered) {
        if (page == nullptr) {
            // A missing page would break the Page index mapping; insert an empty
            // page rather than shifting every later slot.
            page = new QWidget(this);
        }
        pages_->addWidget(page);
    }
    show_page(Page::home);
}

void Backstage::set_command_handler(std::function<void(const QString&)> handler) {
    command_handler_ = std::move(handler);
}

void Backstage::set_close_handler(std::function<void()> handler) {
    close_handler_ = std::move(handler);
}

void Backstage::set_options_handler(std::function<void()> handler) {
    options_handler_ = std::move(handler);
}

void Backstage::set_theme_handler(std::function<void(Theme)> handler) {
    theme_handler_ = std::move(handler);
}

void Backstage::set_info_text(const QString& title, const QString& detail) {
    if (info_title_ != nullptr) {
        info_title_->setText(title);
    }
    if (info_detail_ != nullptr) {
        info_detail_->setText(detail);
    }
}

int Backstage::nav_count() const { return static_cast<int>(nav_buttons_.size()); }

void Backstage::show_page(Page page) { show_page(static_cast<int>(page)); }

void Backstage::show_page(int index) {
    if (pages_ == nullptr || index < 0 || index >= pages_->count()) {
        return;
    }
    pages_->setCurrentIndex(index);
    for (std::size_t i = 0; i < nav_buttons_.size(); ++i) {
        if (nav_buttons_[i] == nullptr) {
            continue;
        }
        if (static_cast<int>(i) == index) {
            nav_buttons_[i]->setChecked(true);
        }
    }
}

QWidget* Backstage::build_page(Page page, const QString& title, const QString& body,
                               const QString& command_label, const QString& command) {
    auto* widget = new QWidget(this);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(36, 28, 36, 28);
    layout->setSpacing(10);
    auto* heading = new QLabel(title, widget);
    heading->setFont(backstage_font(20, true));
    heading->setStyleSheet(QStringLiteral("color: #202020;"));
    layout->addWidget(heading);
    layout->addWidget(make_body(widget, body));
    if (!command_label.isEmpty()) {
        auto* button = new QPushButton(command_label, widget);
        button->setObjectName(QStringLiteral("backstageAction"));
        button->setFixedHeight(30);
        button->setMaximumWidth(260);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background: #2b578a; color: #ffffff; border: none;"
            " padding: 4px 14px; } QPushButton:hover { background: #3a6aa3; }"
            "QPushButton:disabled { background: #c8c8c8; color: #6a6a6a; }"));
        connect(button, &QPushButton::clicked, this, [this, command](bool) {
            if (command_handler_) {
                command_handler_(command);
            }
        });
        layout->addWidget(button);
        if (page == Page::share) {
            // Honest stub: the page works (it exports a local copy); the cloud
            // half is disabled with an inline note rather than faked.
            auto* offline = new QPushButton(QStringLiteral("Share to OneDrive"), widget);
            offline->setFixedHeight(30);
            offline->setMaximumWidth(260);
            offline->setEnabled(false);
            offline->setToolTip(QStringLiteral("not implemented in this port"));
            layout->addWidget(offline);
        }
    }
    layout->addStretch(1);
    return widget;
}

}  // namespace msqt
