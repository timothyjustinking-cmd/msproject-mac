// app_qt/backstage.cpp -- see backstage.h.

#include "backstage.h"

#include <QAbstractButton>
#include <QColor>
#include <QComboBox>
#include <QEnterEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <utility>

#include "shell_style.h"

namespace msqt {
namespace {

// The measured Account capture (project-backstage-account.png, 1903x1033).
const QColor kRailBackground(0xf0, 0xf0, 0xf0);
const QColor kContentBackground(0xf5, 0xf5, 0xf5);
const QColor kNavText(0x24, 0x24, 0x24);
const QColor kNavDisabledText(0x99, 0x99, 0x99);
const QColor kNavSelectedText(0x0f, 0x70, 0x3b);
const QColor kSeparator(0xd1, 0xd1, 0xd1);

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

// The three rail glyphs the clean capture shows, drawn on a 16-unit grid in one
// ink colour (our artwork; nothing is copied from Office).  A disabled render in
// the capture's grey gives the rail entry the measured disabled look.
enum class RailGlyph { home, page, folder };

QPixmap draw_rail_glyph(RailGlyph glyph, const QColor& ink) {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(1.4);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    switch (glyph) {
        case RailGlyph::home: {
            QPainterPath roof;
            roof.moveTo(8.0, 2.0);
            roof.lineTo(2.0, 7.5);
            painter.drawPath(roof);
            painter.drawRect(QRectF(3.5, 7.5, 9.0, 6.5));
            painter.drawRect(QRectF(6.5, 10.5, 3.0, 3.5));
            break;
        }
        case RailGlyph::page: {
            QPainterPath outline;
            outline.moveTo(4.0, 1.5);
            outline.lineTo(9.5, 1.5);
            outline.lineTo(12.5, 4.5);
            outline.lineTo(12.5, 14.5);
            outline.lineTo(4.0, 14.5);
            outline.closeSubpath();
            painter.drawPath(outline);
            QPainterPath fold;
            fold.moveTo(9.5, 1.5);
            fold.lineTo(9.5, 4.5);
            fold.lineTo(12.5, 4.5);
            painter.drawPath(fold);
            break;
        }
        case RailGlyph::folder: {
            QPainterPath outline;
            outline.moveTo(1.5, 5.0);
            outline.lineTo(1.5, 13.5);
            outline.lineTo(14.5, 13.5);
            outline.lineTo(14.5, 5.0);
            outline.lineTo(7.5, 5.0);
            outline.lineTo(6.0, 2.5);
            outline.lineTo(1.5, 2.5);
            outline.closeSubpath();
            painter.drawPath(outline);
            painter.drawLine(QPointF(2.0, 6.5), QPointF(14.0, 6.5));
            break;
        }
    }
    painter.end();
    return pixmap;
}

// One rail entry, painted at the capture's own geometry.  A QToolButton centres
// its text even when the stylesheet asks for text-align:left (measured: Info
// landed at x=74 instead of the capture's 38), so the rail draws its own rows:
// the glyph at x=13 and the label at x=38, both measured from
// project-backstage-account.png.  QAbstractButton still supplies the checkable
// auto-exclusive group, the enabled state and the clicked() signal.
class RailButton final : public QAbstractButton {
  public:
    RailButton(QWidget* parent, const QString& label, bool has_glyph, RailGlyph glyph,
               bool bottom)
        : QAbstractButton(parent),
          has_glyph_(has_glyph),
          glyph_(glyph),
          bottom_(bottom) {
        setText(label);
        setFont(backstage_font(kBodyPointSize));
        setCheckable(true);
        setAutoExclusive(true);
        setFixedHeight(Backstage::kNavEntryHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
    }

    QSize sizeHint() const override { return QSize(Backstage::kRailWidth, Backstage::kNavEntryHeight); }
    QSize minimumSizeHint() const override { return QSize(40, Backstage::kNavEntryHeight); }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        if (underMouse() && isEnabled()) {
            painter.fillRect(rect(), QColor(0xe5, 0xe5, 0xe5));
        }
        const bool selected = isChecked();
        const QColor ink = !isEnabled() ? kNavDisabledText
                                        : (bottom_ && selected ? kNavSelectedText : kNavText);
        QFont row_font = font();
        row_font.setBold(selected);
        painter.setFont(row_font);
        if (has_glyph_) {
            painter.drawPixmap(13, (height() - 16) / 2, draw_rail_glyph(glyph_, ink));
        }
        painter.setPen(ink);
        painter.drawText(QRect(38, 0, width() - 38, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, text());
        if (bottom_ && selected) {
            painter.fillRect(0, 0, 2, height(), kNavSelectedText);
            painter.fillRect(0, 0, width(), 2, QColor(0x00, 0x00, 0x00));
            painter.fillRect(0, height() - 2, width(), 2, QColor(0x00, 0x00, 0x00));
        }
    }

    void enterEvent(QEnterEvent* event) override {
        QAbstractButton::enterEvent(event);
        update();
    }
    void leaveEvent(QEvent* event) override {
        QAbstractButton::leaveEvent(event);
        update();
    }

  private:
    bool has_glyph_ = false;
    RailGlyph glyph_ = RailGlyph::home;
    bool bottom_ = false;
};

// The New page's two template tiles, drawn on a 32-unit grid: a blank page with
// a plus, and two stacked pages for "New from existing project".  Our artwork.
QPixmap draw_template_glyph(bool existing) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(kNavText);
    pen.setWidthF(1.6);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (!existing) {
        QPainterPath page;
        page.moveTo(7.0, 3.0);
        page.lineTo(19.0, 3.0);
        page.lineTo(25.0, 9.0);
        page.lineTo(25.0, 27.0);
        page.lineTo(7.0, 27.0);
        page.closeSubpath();
        painter.drawPath(page);
        QPainterPath fold;
        fold.moveTo(19.0, 3.0);
        fold.lineTo(19.0, 9.0);
        fold.lineTo(25.0, 9.0);
        painter.drawPath(fold);
        painter.setPen(QPen(QColor(0x0f, 0x70, 0x3b), 2.4));
        painter.drawLine(QPointF(16.0, 14.0), QPointF(16.0, 22.0));
        painter.drawLine(QPointF(12.0, 18.0), QPointF(20.0, 18.0));
    } else {
        painter.drawRect(QRectF(4.0, 7.0, 15.0, 20.0));
        painter.drawRect(QRectF(10.0, 4.0, 15.0, 20.0));
    }
    painter.end();
    return pixmap;
}

// A one-pixel rule inset the measured 15 px from the rail's left and 18 px from
// its right (the Account capture's separators at y=207 and y=914).
QWidget* make_separator(QWidget* parent, int top_margin, int bottom_margin) {
    auto* wrapper = new QWidget(parent);
    auto* layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(15, top_margin, 18, bottom_margin);
    layout->setSpacing(0);
    auto* line = new QFrame(wrapper);
    line->setObjectName(QStringLiteral("backstageSeparator"));
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #d1d1d1;"));
    layout->addWidget(line);
    return wrapper;
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

    // --- left rail (W26-4.1: Project's measured light rail) ----------------
    nav_rail_ = new QWidget(this);
    nav_rail_->setObjectName(QStringLiteral("backstageRail"));
    nav_rail_->setFixedWidth(kRailWidth);
    nav_rail_->setStyleSheet(QStringLiteral(
        "QWidget#backstageRail { background: #f0f0f0; }"));
    auto* rail_layout = new QVBoxLayout(nav_rail_);
    rail_layout->setContentsMargins(0, 0, 0, 0);
    rail_layout->setSpacing(0);

    // The circular BACK arrow at the top-left, 21x21 at (10, 14) -- the clean
    // capture's box x=10..30, y=45..65 below its 31-px title bar.  It calls the
    // same close handler the Close page uses (Escape does the same).
    back_button_ = new QToolButton(nav_rail_);
    back_button_->setObjectName(QStringLiteral("backstageBack"));
    back_button_->setText(QString::fromUtf8("\xE2\x86\x90"));  // U+2190 leftwards arrow
    back_button_->setToolTip(QStringLiteral("Back (Esc)"));
    back_button_->setAccessibleName(QStringLiteral("Back"));
    back_button_->setCursor(Qt::PointingHandCursor);
    back_button_->setFixedSize(kBackArrowSize, kBackArrowSize);
    back_button_->setFont(backstage_font(9, false));
    back_button_->setStyleSheet(QStringLiteral(
        "QToolButton#backstageBack { color: #4a4a4a; background: transparent;"
        " border: 1px solid #4a4a4a; border-radius: 10px; }"
        "QToolButton#backstageBack:hover { background: #e5e5e5; }"));
    connect(back_button_, &QToolButton::clicked, this, [this](bool) {
        if (close_handler_) {
            close_handler_();
        }
    });
    auto* back_row = new QHBoxLayout();
    back_row->setContentsMargins(kBackArrowLeft, kBackArrowTop, 0, 0);
    back_row->setSpacing(0);
    back_row->addWidget(back_button_);
    back_row->addStretch(1);
    rail_layout->addLayout(back_row);
    // The capture's Home text centre is 44 px below the arrow's centre; the
    // arrow occupies 14..35, so the first 40-px entry starts at y=51.
    rail_layout->addSpacing(16);

    struct NavSpec {
        const char* label;
        Page page;
        RailGlyph glyph;
        bool has_glyph;
    };
    const NavSpec main_nav[] = {
        {"Home", Page::home, RailGlyph::home, true},
        {"New", Page::create_new, RailGlyph::page, true},
        {"Open", Page::open, RailGlyph::folder, true},
    };
    for (const NavSpec& spec : main_nav) {
        auto* button = new RailButton(nav_rail_, QString::fromUtf8(spec.label), spec.has_glyph,
                                      spec.glyph, false);
        button->setObjectName(QStringLiteral("backstageNav"));
        const int index = static_cast<int>(spec.page);
        connect(button, &QAbstractButton::clicked, this,
                [this, index](bool) { show_page(index); });
        rail_layout->addWidget(button);
        nav_buttons_.push_back(button);
    }

    rail_layout->addWidget(make_separator(nav_rail_, 4, 4));

    // Info..Close are text-only; their label starts at the same measured x=38.
    const NavSpec text_nav[] = {
        {"Info", Page::info, RailGlyph::home, false},
        {"Save", Page::save, RailGlyph::home, false},
        {"Save As", Page::save_as, RailGlyph::home, false},
        {"Print", Page::print, RailGlyph::home, false},
        {"Share", Page::share, RailGlyph::home, false},
        {"Export", Page::export_file, RailGlyph::home, false},
        {"Close", Page::close_file, RailGlyph::home, false},
    };
    for (const NavSpec& spec : text_nav) {
        auto* button = new RailButton(nav_rail_, QString::fromUtf8(spec.label), false,
                                      RailGlyph::home, false);
        button->setObjectName(QStringLiteral("backstageNav"));
        const int index = static_cast<int>(spec.page);
        connect(button, &QAbstractButton::clicked, this,
                [this, index](bool) { show_page(index); });
        rail_layout->addWidget(button);
        nav_buttons_.push_back(button);
    }

    rail_layout->addStretch(1);

    // Account sits under Close at the bottom, above the pinned Options entry.
    rail_layout->addWidget(make_separator(nav_rail_, 1, 5));
    auto* account = new RailButton(nav_rail_, QStringLiteral("Account"), false,
                                   RailGlyph::home, true);
    account->setObjectName(QStringLiteral("backstageNavBottom"));
    connect(account, &QAbstractButton::clicked, this,
            [this](bool) { show_page(Page::account); });
    rail_layout->addWidget(account);
    nav_buttons_.push_back(account);

    options_button_ = new RailButton(nav_rail_, QStringLiteral("Options"), false,
                                     RailGlyph::home, false);
    options_button_->setObjectName(QStringLiteral("backstageOptions"));
    options_button_->setCheckable(false);
    options_button_->setAutoExclusive(false);
    connect(options_button_, &QAbstractButton::clicked, this, [this](bool) {
        if (options_handler_) {
            options_handler_();
        }
    });
    rail_layout->addWidget(options_button_);
    nav_buttons_.push_back(options_button_);
    outer->addWidget(nav_rail_);

    // --- pages -------------------------------------------------------------
    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("backstagePages"));
    pages_->setStyleSheet(QStringLiteral("QStackedWidget { background: #f5f5f5; }"));
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
        // W26-4.3: the New page is built by build_new_page() below (templates,
        // the disabled online search and the local Recent list).
        // W26-4.4: Open and Save As are built by build_location_page() (Recent,
        // This PC and Browse; OneDrive/SharePoint laid out and disabled).
        {Page::save, "Save",
         "Save the pending edits through the shared writer. A new target path is "
         "asked for when the file is not one this app wrote.",
         "Save the pending edits", "save"},
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
    std::vector<QWidget*> ordered(static_cast<std::size_t>(Page::count), nullptr);
    for (const PageSpec& spec : page_specs) {
        QWidget* page = build_page(spec.page, QString::fromUtf8(spec.title),
                                   QString::fromUtf8(spec.body),
                                   QString::fromUtf8(spec.command_label),
                                   QString::fromUtf8(spec.command));
        ordered[static_cast<std::size_t>(spec.page)] = page;
        page_widgets_.push_back(page);
    }

    // W26-4.3: New -- two template tiles, the disabled online search and the
    // local Recent list (the capture's page; no online templates in this port).
    {
        QWidget* page = build_new_page();
        ordered[static_cast<std::size_t>(Page::create_new)] = page;
        page_widgets_.push_back(page);
    }

    // W26-4.4: Open and Save As -- Recent, This PC, Browse; the cloud entries
    // are laid out and disabled.
    {
        QWidget* page = build_location_page(Page::open);
        ordered[static_cast<std::size_t>(Page::open)] = page;
        page_widgets_.push_back(page);
        QWidget* save_as_page = build_location_page(Page::save_as);
        ordered[static_cast<std::size_t>(Page::save_as)] = save_as_page;
        page_widgets_.push_back(save_as_page);
    }

    // Info: honest local project facts, refreshed by set_info_text().
    {
        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(41, 27, 41, 27);
        layout->setSpacing(10);
        auto* title = new QLabel(QStringLiteral("Info"), page);
        title->setFont(backstage_font(15, true));
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

    // Account: the page shell is W26-4.1; W26-4.2 fills its content.
    {
        QWidget* page = build_account_page();
        ordered[static_cast<std::size_t>(Page::account)] = page;
        page_widgets_.push_back(page);
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

void Backstage::set_theme_choice_handler(std::function<void(ThemeChoice)> handler) {
    theme_choice_handler_ = std::move(handler);
}

void Backstage::set_about_handler(std::function<void()> handler) {
    about_handler_ = std::move(handler);
}

void Backstage::set_recent_files(const QStringList& files) {
    recent_files_ = files;
    rebuild_recent_lists();
}

void Backstage::set_recent_open_handler(std::function<void(const QString&)> handler) {
    recent_open_handler_ = std::move(handler);
}

void Backstage::rebuild_recent_lists() {
    auto fill = [this](QListWidget* list) {
        if (list == nullptr) {
            return;
        }
        list->clear();
        for (const QString& path : recent_files_) {
            auto* item = new QListWidgetItem(QFileInfo(path).fileName(), list);
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
        }
    };
    fill(new_recent_);
    fill(open_recent_);
    fill(save_as_recent_);
}

void Backstage::set_info_text(const QString& title, const QString& detail) {
    if (info_title_ != nullptr) {
        info_title_->setText(title);
    }
    if (info_detail_ != nullptr) {
        info_detail_->setText(detail);
    }
}

void Backstage::set_account_info(const QString& product, const QString& version,
                                 const QString& detail) {
    if (product_label_ != nullptr) {
        product_label_->setText(product);
    }
    if (version_label_ != nullptr) {
        version_label_->setText(version);
    }
    if (account_detail_ != nullptr && !detail.isEmpty()) {
        account_detail_->setText(detail);
    }
}

void Backstage::set_theme_choice(ThemeChoice choice) {
    if (theme_combo_ == nullptr) {
        return;
    }
    const int index = static_cast<int>(choice);
    if (index < 0 || index >= theme_combo_->count()) {
        return;
    }
    const QSignalBlocker blocker(theme_combo_);
    theme_combo_->setCurrentIndex(index);
}

void Backstage::set_document_state(bool has_document) {
    has_document_ = has_document;
    // The clean Account capture (no document) greys Save..Close and keeps
    // Home/New/Open/Info/Account/Options active; that is the measured rule.
    const int first_stateful = static_cast<int>(Page::save);
    const int last_stateful = static_cast<int>(Page::close_file);
    for (int index = first_stateful; index <= last_stateful; ++index) {
        if (index < static_cast<int>(nav_buttons_.size()) &&
            nav_buttons_[static_cast<std::size_t>(index)] != nullptr) {
            nav_buttons_[static_cast<std::size_t>(index)]->setEnabled(has_document);
        }
    }
}

int Backstage::nav_count() const { return static_cast<int>(nav_buttons_.size()); }

QSize Backstage::minimumSizeHint() const { return QSize(320, 320); }

void Backstage::show_page(Page page) { show_page(static_cast<int>(page)); }

void Backstage::show_page(int index) {
    // The rail's last entry is the Options ACTION, not a page.
    if (index == static_cast<int>(Page::count)) {
        if (options_handler_) {
            options_handler_();
        }
        return;
    }
    if (pages_ == nullptr || index < 0 || index >= pages_->count()) {
        return;
    }
    pages_->setCurrentIndex(index);
    set_nav_checked(index);
}

void Backstage::set_nav_checked(int page_index) {
    for (std::size_t i = 0; i < nav_buttons_.size(); ++i) {
        if (nav_buttons_[i] == nullptr || nav_buttons_[i] == options_button_) {
            continue;
        }
        if (static_cast<int>(i) == page_index) {
            nav_buttons_[i]->setChecked(true);
        }
    }
}

QWidget* Backstage::build_page(Page page, const QString& title, const QString& body,
                               const QString& command_label, const QString& command) {
    auto* widget = new QWidget(this);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(41, 27, 41, 27);
    layout->setSpacing(10);
    auto* heading = new QLabel(title, widget);
    heading->setFont(backstage_font(15, true));
    heading->setStyleSheet(QStringLiteral("color: #202020;"));
    layout->addWidget(heading);
    layout->addWidget(make_body(widget, body));
    if (!command_label.isEmpty()) {
        auto* button = new QPushButton(command_label, widget);
        button->setObjectName(QStringLiteral("backstageAction"));
        button->setFixedHeight(26);
        button->setMaximumWidth(260);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background: #ffffff; color: #242424;"
            " border: 1px solid #b8b8b8; padding: 3px 12px; }"
            "QPushButton:hover { background: #eef5fb; }"
            "QPushButton:disabled { background: #f0f0f0; color: #999999;"
            " border-color: #d0d0d0; }"));
        connect(button, &QPushButton::clicked, this, [this, command](bool) {
            if (command_handler_) {
                command_handler_(command);
            }
        });
        layout->addWidget(button);
        if (page == Page::export_file) {
            // Wave 23 (W23-10 / A-5, Q17): Save as PDF's Project home is
            // File > Export > Create PDF/XPS.  It had been ribbon-only and then
            // removed with the ribbon, leaving it mouse-unreachable.
            auto* create_pdf = new QPushButton(QStringLiteral("Create PDF/XPS..."), widget);
            create_pdf->setObjectName(QStringLiteral("backstageAction"));
            create_pdf->setFixedHeight(26);
            create_pdf->setMaximumWidth(260);
            create_pdf->setStyleSheet(button->styleSheet());
            connect(create_pdf, &QPushButton::clicked, this, [this](bool) {
                if (command_handler_) {
                    command_handler_(QStringLiteral("save_as_pdf"));
                }
            });
            layout->addWidget(create_pdf);
        }
        if (page == Page::share) {
            // Honest stub: the page works (it exports a local copy); the cloud
            // half is disabled with an inline note rather than faked.
            auto* offline = new QPushButton(QStringLiteral("Share to OneDrive"), widget);
            offline->setFixedHeight(26);
            offline->setMaximumWidth(260);
            offline->setEnabled(false);
            offline->setToolTip(QStringLiteral("not implemented in this port"));
            layout->addWidget(offline);
        }
    }
    layout->addStretch(1);
    return widget;
}

QWidget* Backstage::build_new_page() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(41, 27, 41, 27);
    layout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("New"), page);
    title->setFont(backstage_font(15, true));
    title->setStyleSheet(QStringLiteral("color: #202020;"));
    layout->addWidget(title);

    auto make_tile = [this, page](const QString& label, bool existing,
                                  const char* object_name,
                                  const char* command) -> QToolButton* {
        auto* tile = new QToolButton(page);
        tile->setObjectName(QString::fromUtf8(object_name));
        tile->setText(label);
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tile->setIcon(QIcon(draw_template_glyph(existing)));
        tile->setIconSize(QSize(32, 32));
        tile->setFixedSize(190, 96);
        tile->setFont(backstage_font(kBodyPointSize));
        tile->setToolTip(label);
        tile->setStyleSheet(QStringLiteral(
            "QToolButton { background: #ffffff; border: 1px solid #c8c8c8;"
            " color: #242424; padding: 4px; }"
            "QToolButton:hover { background: #eef5fb; border-color: #9db8d2; }"));
        connect(tile, &QToolButton::clicked, this, [this, command](bool) {
            if (command_handler_) {
                command_handler_(QString::fromUtf8(command));
            }
        });
        return tile;
    };

    auto* tiles = new QHBoxLayout();
    tiles->setContentsMargins(0, 0, 0, 0);
    tiles->setSpacing(20);
    // The capture's two local templates.  "New from existing project" opens a
    // project as the starting point (the reader never modifies it in place);
    // the online/SharePoint templates the capture also shows are not offered.
    new_blank_ = make_tile(QStringLiteral("Blank Project"), false, "newBlankProject",
                           "new");
    new_existing_ = make_tile(QStringLiteral("New from existing project"), true,
                              "newFromExisting", "new_from_existing");
    tiles->addWidget(new_blank_);
    tiles->addWidget(new_existing_);
    tiles->addStretch(1);
    layout->addLayout(tiles);

    auto* search_row = new QHBoxLayout();
    search_row->setContentsMargins(0, 0, 0, 0);
    search_row->setSpacing(8);
    online_search_ = new QLineEdit(page);
    online_search_->setObjectName(QStringLiteral("newOnlineSearch"));
    online_search_->setPlaceholderText(QStringLiteral("Search for online templates"));
    online_search_->setFixedWidth(300);
    online_search_->setFixedHeight(24);
    online_search_->setEnabled(false);
    online_search_->setToolTip(QStringLiteral("not implemented in this port"));
    search_row->addWidget(online_search_);
    search_row->addStretch(1);
    layout->addLayout(search_row);
    online_note_ = new QLabel(
        QStringLiteral("Online templates are not available in this port; the search is "
                       "disabled."),
        page);
    online_note_->setObjectName(QStringLiteral("newOnlineNote"));
    online_note_->setFont(backstage_font(kBodyPointSize));
    online_note_->setStyleSheet(QStringLiteral("color: #4a4a4a;"));
    layout->addWidget(online_note_);

    auto* recent_title = new QLabel(QStringLiteral("Recent"), page);
    recent_title->setFont(backstage_font(kBodyPointSize, true));
    recent_title->setStyleSheet(QStringLiteral("color: #202020;"));
    layout->addWidget(recent_title);
    new_recent_ = new QListWidget(page);
    new_recent_->setObjectName(QStringLiteral("newRecentList"));
    new_recent_->setFixedHeight(240);
    new_recent_->setFont(backstage_font(kBodyPointSize));
    connect(new_recent_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) {
                if (item != nullptr && recent_open_handler_) {
                    recent_open_handler_(item->data(Qt::UserRole).toString());
                }
            });
    layout->addWidget(new_recent_);
    layout->addWidget(make_body(
        page, QStringLiteral("Recent lists local files only. Nothing here is synced to "
                             "a cloud service.")));
    layout->addStretch(1);
    return page;
}

QWidget* Backstage::build_location_page(Page page) {
    const bool save_as = page == Page::save_as;
    auto* widget = new QWidget(this);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(41, 27, 41, 27);
    layout->setSpacing(10);
    auto* heading = new QLabel(save_as ? QStringLiteral("Save As") : QStringLiteral("Open"),
                               widget);
    heading->setFont(backstage_font(15, true));
    heading->setStyleSheet(QStringLiteral("color: #202020;"));
    layout->addWidget(heading);
    layout->addWidget(make_body(
        widget,
        save_as ? QStringLiteral("Write the project to a new .mpp file, leaving the opened "
                                 "file untouched. This port writes local files only.")
                : QStringLiteral("Open an .mpp file from this computer. The file is read "
                                 "through the shared portable reader; it is never modified "
                                 "in place.")));

    auto* columns = new QHBoxLayout();
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(24);
    layout->addLayout(columns, 1);

    auto* left = new QWidget(widget);
    left->setObjectName(save_as ? QStringLiteral("saveAsLocations")
                                : QStringLiteral("openLocations"));
    left->setFixedWidth(220);
    auto* left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(6);
    auto section = [left](const QString& text) {
        auto* label = new QLabel(text, left);
        label->setFont(backstage_font(kBodyPointSize, true));
        label->setStyleSheet(QStringLiteral("color: #202020;"));
        return label;
    };
    auto location = [left](const QString& text, const char* object_name, bool enabled) {
        auto* button = new QPushButton(text, left);
        button->setObjectName(QString::fromUtf8(object_name));
        button->setFlat(true);
        button->setFixedHeight(28);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { border: none; text-align: left; color: #242424;"
            " padding-left: 8px; background: transparent; }"
            "QPushButton:hover { background: #e5e5e5; }"
            "QPushButton:disabled { color: #999999; }"));
        button->setEnabled(enabled);
        if (!enabled) {
            button->setToolTip(QStringLiteral("not implemented in this port"));
        }
        return button;
    };
    auto command_button = [this](QAbstractButton* button, const char* command) {
        connect(button, &QAbstractButton::clicked, this, [this, command](bool) {
            if (command_handler_) {
                command_handler_(QString::fromUtf8(command));
            }
        });
    };

    left_layout->addWidget(section(QStringLiteral("Recent")));
    left_layout->addWidget(section(QStringLiteral("Personal")));
    QAbstractButton* onedrive =
        location(QStringLiteral("OneDrive - Personal"),
                 save_as ? "saveAsOneDrive" : "openOneDrive", false);
    left_layout->addWidget(onedrive);
    QAbstractButton* sharepoint =
        location(QStringLiteral("SharePoint"),
                 save_as ? "saveAsSharePoint" : "openSharePoint", false);
    left_layout->addWidget(sharepoint);
    left_layout->addWidget(section(QStringLiteral("Other locations")));
    QAbstractButton* this_pc = location(QStringLiteral("This PC"),
                                       save_as ? "saveAsThisPc" : "openThisPc", true);
    command_button(this_pc, save_as ? "save_as_this_pc" : "open_this_pc");
    left_layout->addWidget(this_pc);
    left_layout->addWidget(location(QStringLiteral("Add a Place"),
                                    save_as ? "saveAsAddPlace" : "openAddPlace", false));
    QAbstractButton* browse = location(QStringLiteral("Browse"),
                                      save_as ? "saveAsBrowse" : "openBrowse", true);
    command_button(browse, save_as ? "save_as" : "open");
    left_layout->addWidget(browse);
    left_layout->addStretch(1);
    columns->addWidget(left);

    auto* right = new QWidget(widget);
    auto* right_layout = new QVBoxLayout(right);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(6);
    auto* recent_title = new QLabel(QStringLiteral("Recent"), right);
    recent_title->setFont(backstage_font(kBodyPointSize, true));
    recent_title->setStyleSheet(QStringLiteral("color: #202020;"));
    right_layout->addWidget(recent_title);
    auto* list = new QListWidget(right);
    list->setObjectName(save_as ? QStringLiteral("saveAsRecentList")
                                : QStringLiteral("openRecentList"));
    list->setFont(backstage_font(kBodyPointSize));
    list->setMinimumHeight(240);
    if (save_as) {
        save_as_recent_ = list;
        right_layout->addWidget(make_body(
            right, QStringLiteral("Save As writes a new target file; choose Browse or "
                                  "This PC. The recent list is local and shown for "
                                  "reference.")));
    } else {
        open_recent_ = list;
        connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            if (item != nullptr && recent_open_handler_) {
                recent_open_handler_(item->data(Qt::UserRole).toString());
            }
        });
        right_layout->addWidget(make_body(
            right, QStringLiteral("Double-click a recent local file to open it; nothing "
                                  "here is synced to a cloud service.")));
    }
    right_layout->addWidget(list);
    right_layout->addStretch(1);
    columns->addWidget(right, 1);
    layout->addStretch(0);
    return widget;
}

QWidget* Backstage::build_account_page() {
    // The clean Account capture's two columns: the left starts at the page's
    // 41-px margin, the right at x=494 (41 + the measured 453-px left column).
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(41, 27, 41, 27);
    layout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("Account"), page);
    title->setFont(backstage_font(15, true));
    title->setStyleSheet(QStringLiteral("color: #202020;"));
    layout->addWidget(title);

    auto* columns = new QHBoxLayout();
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(0);
    layout->addLayout(columns);

    auto* left = new QWidget(page);
    left->setObjectName(QStringLiteral("accountLeftColumn"));
    left->setFixedWidth(453);
    account_left_column_ = left;
    auto* left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(8);

    auto section = [left](const QString& text) {
        auto* label = new QLabel(text, left);
        label->setFont(backstage_font(kBodyPointSize, true));
        label->setStyleSheet(QStringLiteral("color: #202020;"));
        return label;
    };
    auto field_label = [left](const QString& text) {
        auto* label = new QLabel(text, left);
        label->setFont(backstage_font(kBodyPointSize));
        label->setStyleSheet(QStringLiteral("color: #3a3a3a;"));
        return label;
    };
    auto secondary = [left](const QString& text, bool enabled, bool link_style = false) {
        auto* button = new QPushButton(text, left);
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(24);
        button->setMaximumWidth(377);
        button->setStyleSheet(link_style
                                  ? QStringLiteral(
                                        "QPushButton { border: none; text-align: left;"
                                        " color: #1a5c8f; background: transparent; }"
                                        "QPushButton:disabled { color: #999999; }")
                                  : QStringLiteral(
                                        "QPushButton { background: #ffffff; color: #242424;"
                                        " border: 1px solid #b8b8b8; padding: 2px 10px; }"
                                        "QPushButton:disabled { background: #f0f0f0;"
                                        " color: #999999; border-color: #d0d0d0; }"));
        button->setEnabled(enabled);
        if (!enabled) {
            button->setToolTip(QStringLiteral("not implemented in this port"));
        }
        return button;
    };

    left_layout->addWidget(section(QStringLiteral("User Information")));
    {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(10);
        auto* avatar = new QLabel(QStringLiteral("MS"), left);
        avatar->setObjectName(QStringLiteral("accountAvatar"));
        avatar->setFixedSize(28, 28);
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setFont(backstage_font(kBodyPointSize, true));
        avatar->setStyleSheet(QStringLiteral(
            "background: #2b578a; color: #ffffff; border-radius: 14px;"));
        row->addWidget(avatar);
        auto* identity = new QVBoxLayout();
        identity->setContentsMargins(0, 0, 0, 0);
        identity->setSpacing(0);
        auto* name = new QLabel(QStringLiteral("Local session"), left);
        name->setFont(backstage_font(kBodyPointSize, true));
        identity->addWidget(name);
        auto* note = new QLabel(
            QStringLiteral("No Microsoft account is connected."), left);
        note->setFont(backstage_font(kBodyPointSize));
        note->setStyleSheet(QStringLiteral("color: #4a4a4a;"));
        identity->addWidget(note);
        row->addLayout(identity, 1);
        left_layout->addLayout(row);
    }
    {
        auto* links = new QHBoxLayout();
        links->setContentsMargins(0, 0, 0, 0);
        links->setSpacing(12);
        links->addWidget(secondary(QStringLiteral("Sign out"), false, true));
        links->addWidget(secondary(QStringLiteral("Switch account"), false, true));
        links->addStretch(1);
        left_layout->addLayout(links);
    }
    left_layout->addWidget(section(QStringLiteral("Account Privacy")));
    left_layout->addWidget(secondary(QStringLiteral("Manage Settings"), false));
    left_layout->addWidget(field_label(QStringLiteral("Office Background:")));
    {
        auto* background = new QComboBox(left);
        background->addItem(QStringLiteral("No Background"));
        background->setFixedWidth(377);
        background->setFixedHeight(24);
        background->setEnabled(false);
        background->setToolTip(QStringLiteral("not implemented in this port"));
        left_layout->addWidget(background);
    }
    left_layout->addWidget(field_label(QStringLiteral("Office Theme:")));
    theme_combo_ = new QComboBox(left);
    theme_combo_->setObjectName(QStringLiteral("accountThemeCombo"));
    theme_combo_->setFixedWidth(377);
    theme_combo_->setFixedHeight(24);
    auto* theme_model = qobject_cast<QStandardItemModel*>(theme_combo_->model());
    const std::vector<ThemeChoiceSpec>& choices = office_theme_choices();
    for (std::size_t index = 0; index < choices.size(); ++index) {
        theme_combo_->addItem(QString::fromUtf8(choices[index].name));
        if (!choices[index].implemented) {
            // W26-4.2: the five names are laid out; only the palettes the port
            // paints are enabled (decision 9's exact tooltip on the others).
            theme_combo_->setItemData(static_cast<int>(index),
                                      QStringLiteral("not implemented in this port"),
                                      Qt::ToolTipRole);
            if (theme_model != nullptr) {
                if (QStandardItem* item = theme_model->item(static_cast<int>(index))) {
                    item->setEnabled(false);
                }
            }
        }
    }
    connect(theme_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0 || index >= static_cast<int>(office_theme_choices().size())) {
            return;
        }
        const ThemeChoice choice = static_cast<ThemeChoice>(index);
        if (theme_choice_handler_ != nullptr && theme_choice_implemented(choice)) {
            theme_choice_handler_(choice);
        }
    });
    left_layout->addWidget(theme_combo_);
    left_layout->addWidget(section(QStringLiteral("Connected Services:")));
    left_layout->addWidget(
        secondary(QStringLiteral("OneDrive - Personal"), false, true));
    left_layout->addWidget(secondary(QStringLiteral("Add a service"), false));
    left_layout->addStretch(1);
    columns->addWidget(left);

    auto* right = new QWidget(page);
    right->setObjectName(QStringLiteral("accountRightColumn"));
    account_right_column_ = right;
    auto* right_layout = new QVBoxLayout(right);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(8);
    right_layout->addWidget(section(QStringLiteral("Product Information")));
    product_label_ = new QLabel(QStringLiteral("MS Project Shell"), right);
    product_label_->setObjectName(QStringLiteral("accountProduct"));
    product_label_->setFont(backstage_font(kBodyPointSize + 2, false));
    right_layout->addWidget(product_label_);
    version_label_ = new QLabel(QStringLiteral("Version 0.1.0"), right);
    version_label_->setObjectName(QStringLiteral("accountVersion"));
    version_label_->setFont(backstage_font(kBodyPointSize));
    right_layout->addWidget(version_label_);
    account_detail_ = make_body(
        right, QStringLiteral("This product contains the portable reader, scheduler and "
                              "writer of this port; it is not Microsoft Project and is not "
                              "activated by any Microsoft account."));
    right_layout->addWidget(account_detail_);
    right_layout->addWidget(section(QStringLiteral("About")));
    right_layout->addWidget(make_body(
        right, QStringLiteral("Learn about this port's reader, scheduler, writer and the "
                              "three-platform Qt shell.")));
    about_button_ = secondary(QStringLiteral("About MS Project Shell..."), true);
    about_button_->setObjectName(QStringLiteral("accountAbout"));
    about_button_->setMaximumWidth(240);
    connect(about_button_, &QAbstractButton::clicked, this, [this](bool) {
        if (about_handler_) {
            about_handler_();
        }
    });
    right_layout->addWidget(about_button_);
    right_layout->addWidget(
        secondary(QStringLiteral("Switch License"), false));
    right_layout->addStretch(1);
    columns->addWidget(right, 1);

    layout->addStretch(1);
    return page;
}

}  // namespace msqt
