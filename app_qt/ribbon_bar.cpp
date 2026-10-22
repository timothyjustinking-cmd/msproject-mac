// app_qt/ribbon_bar.cpp -- see ribbon_bar.h.

#include "ribbon_bar.h"

#include <QColor>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPainter>
#include <QPen>
#include <QSizePolicy>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>
#include <vector>

#include "shell_style.h"

namespace msqt {
namespace {

struct ButtonSpec {
    const char* label;
    app::MenuAction action;
};

struct GroupSpec {
    const char* title;
    std::vector<ButtonSpec> buttons;
};

struct TabSpec {
    const char* title;
    bool contextual;
    std::vector<GroupSpec> groups;
};

// The ribbon structure and order: Task first (the reference's active tab), then
// Resource, Report, Project, View, Help, and the contextual Gantt Chart Format.
// Labels are the product's group captions; the buttons are the commands that
// group offers.  Only the modelled ones carry a MenuAction; the rest are shown
// and report as "not wired" when clicked, so no button is silent.
const std::vector<TabSpec>& tab_specs() {
    static const std::vector<TabSpec> kTabs = {
        {"Task",
         false,
         {
             {"View", {{"Gantt Chart", app::MenuAction::none}}},
             {"Clipboard",
              {{"Paste", app::MenuAction::paste},
               {"Cut", app::MenuAction::cut},
               {"Copy", app::MenuAction::copy},
               {"Format Painter", app::MenuAction::none}}},
             {"Font", {{"Calibri", app::MenuAction::none},
                       {"11", app::MenuAction::none},
                       {"B", app::MenuAction::none},
                       {"I", app::MenuAction::none},
                       {"U", app::MenuAction::none}}},
             {"Schedule",
              {{"Mark on Track", app::MenuAction::none},
               {"Respect Links", app::MenuAction::none},
               {"Inactivate", app::MenuAction::none}}},
             {"Tasks",
              {{"Manually Schedule", app::MenuAction::none},
               {"Auto Schedule", app::MenuAction::none},
               {"Inspect", app::MenuAction::none},
               {"Move", app::MenuAction::none},
               {"Mode", app::MenuAction::none}}},
             {"Insert",
              {{"Task", app::MenuAction::insert_task},
               {"Summary", app::MenuAction::none},
               {"Milestone", app::MenuAction::none},
               {"Deliverable", app::MenuAction::none}}},
             {"Properties",
              {{"Information", app::MenuAction::task_fields},
               {"Notes", app::MenuAction::none},
               {"Details", app::MenuAction::none},
               {"Add to Timeline", app::MenuAction::none}}},
             {"Editing",
              {{"Scroll to Task", app::MenuAction::none},
               {"Find", app::MenuAction::show_filter},
               {"Clear", app::MenuAction::clear_filter},
               {"Fill", app::MenuAction::none}}},
         }},
        {"Resource",
         false,
         {
             {"View", {{"Resource Sheet", app::MenuAction::view_resource_sheet},
                       {"Resource Usage", app::MenuAction::view_resource_usage}}},
             {"Insert", {{"Add Resources", app::MenuAction::insert_resource},
                         {"Assign Resources", app::MenuAction::none}}},
             {"Properties", {{"Information", app::MenuAction::none},
                             {"Resource Notes", app::MenuAction::none}}},
         }},
        {"Report",
         false,
         {
             {"Project", {{"Reports", app::MenuAction::none},
                          {"New Report", app::MenuAction::none}}},
             {"Export", {{"Save as PDF", app::MenuAction::save_as_pdf},
                         {"Export PDF (Sheet)", app::MenuAction::export_pdf_sheet},
                         {"Export PDF (Gantt)", app::MenuAction::export_pdf_gantt}}},
         }},
        {"Project",
         false,
         {
             {"Properties", {{"Project Information", app::MenuAction::none},
                             {"Change Working Time", app::MenuAction::none}}},
             {"Status", {{"Update Project", app::MenuAction::none},
                         {"Update Tasks", app::MenuAction::none}}},
             {"Insert", {{"Insert Project", app::MenuAction::none}}},
         }},
        {"View",
         false,
         {
             {"Task Views", {{"Gantt Chart", app::MenuAction::none},
                             {"Task Sheet", app::MenuAction::view_task_sheet}}},
             {"Zoom", {{"Zoom In", app::MenuAction::zoom_in},
                       {"Zoom Out", app::MenuAction::zoom_out},
                       {"Days", app::MenuAction::zoom_day},
                       {"Weeks", app::MenuAction::zoom_week},
                       {"Months", app::MenuAction::zoom_month}}},
             {"Data", {{"Filter", app::MenuAction::show_filter},
                       {"Clear Filter", app::MenuAction::clear_filter},
                       {"Collapse All", app::MenuAction::collapse_all},
                       {"Expand All", app::MenuAction::expand_all}}},
         }},
        {"Help",
         false,
         {
             {"Help", {{"Help", app::MenuAction::none},
                       {"Keyboard Shortcuts", app::MenuAction::keyboard_shortcuts},
                       {"About", app::MenuAction::about}}},
         }},
        {"Gantt Chart Format",
         true,
         {
             {"Format", {{"Bar Styles", app::MenuAction::none},
                         {"Gridlines", app::MenuAction::none},
                         {"Layout", app::MenuAction::none}}},
             {"Show/Hide", {{"Columns", app::MenuAction::none},
                            {"Shading", app::MenuAction::toggle_shading}}},
         }},
    };
    return kTabs;
}

QFont ribbon_font(int point_size, bool bold = false) {
    QFont font;
    font.setPointSize(point_size);
    font.setBold(bold);
    return font;
}

}  // namespace

RibbonBar::RibbonBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ribbon"));
    setFixedHeight(kRibbonHeight + 1);
    // Ignored horizontally: the ribbon's natural width is the full 1920-px
    // reference's group row, far wider than the 900-px test window.  Ignoring
    // the size hint keeps the WINDOW at 900x560 (the pinned render size) while
    // the groups keep their order; anything past the right edge is clipped,
    // exactly as a ribbon does at a narrow width.
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMinimumWidth(0);
    setAutoFillBackground(false);

    // One stylesheet: tabs look like tabs, group captions are small and grey,
    // and the buttons are flat text until hovered.
    setStyleSheet(QStringLiteral(
        "QToolButton#ribbonTab { border: none; padding: 2px 10px; color: #232323;"
        " background: transparent; }"
        "QToolButton#ribbonTab:checked { background: #ffffff; border: 1px solid #d0d0d0;"
        " border-bottom: 1px solid #ffffff; }"
        "QToolButton#ribbonTabContextual { border: none; padding: 2px 10px; color: #2f6b3a;"
        " background: transparent; }"
        "QToolButton#ribbonTabContextual:checked { background: #ffffff;"
        " border: 1px solid #d0d0d0; border-bottom: 1px solid #ffffff; }"
        "QToolButton#ribbonButton { border: 1px solid transparent; padding: 1px 4px;"
        " color: #232323; background: transparent; }"
        "QToolButton#ribbonButton:hover { background: #e8eef5; border: 1px solid #b9cde3; }"
        "QLabel#ribbonGroupLabel { color: #6a6a6a; }"
        "QAbstractButton { border-radius: 2px; }"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    tab_row_ = new QWidget(this);
    tab_row_->setFixedHeight(kRibbonTabHeight);
    auto* tab_layout = new QHBoxLayout(tab_row_);
    tab_layout->setContentsMargins(8, 0, 8, 0);
    tab_layout->setSpacing(0);
    outer->addWidget(tab_row_);

    auto* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Plain);
    rule->setFixedHeight(1);
    rule->setStyleSheet(QStringLiteral("background: #d0d0d0;"));
    outer->addWidget(rule);

    page_ = new QWidget(this);
    page_->setFixedHeight(kRibbonGroupHeight);
    page_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    page_layout_ = new QHBoxLayout(page_);
    page_layout_->setContentsMargins(8, 4, 8, 0);
    page_layout_->setSpacing(0);
    outer->addWidget(page_);
    outer->addStretch(0);

    build_tabs();
    build_page(0);
}

RibbonBar::~RibbonBar() = default;

QSize RibbonBar::sizeHint() const { return QSize(900, kRibbonHeight + 1); }

QSize RibbonBar::minimumSizeHint() const { return QSize(320, kRibbonHeight + 1); }

void RibbonBar::set_action_handler(std::function<void(app::MenuAction)> handler) {
    handler_ = std::move(handler);
}

void RibbonBar::build_tabs() {
    auto* layout = static_cast<QHBoxLayout*>(tab_row_->layout());
    const std::vector<TabSpec>& tabs = tab_specs();
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        auto* button = new QToolButton(tab_row_);
        button->setObjectName(tabs[index].contextual ? QStringLiteral("ribbonTabContextual")
                                                     : QStringLiteral("ribbonTab"));
        button->setText(QString::fromUtf8(tabs[index].title));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setChecked(index == 0);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFont(ribbon_font(kBodyPointSize - 1));
        button->setFixedHeight(kRibbonTabHeight - 4);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, this,
                [this, tab = static_cast<int>(index)](bool) { build_page(tab); });
        layout->addWidget(button);
    }
    layout->addStretch(1);

    // The reference's right-hand "Tell me" box; a label is enough for structure.
    auto* tell_me = new QLabel(QStringLiteral("Tell me what you want to do"), tab_row_);
    tell_me->setFont(ribbon_font(kBodyPointSize - 1));
    tell_me->setStyleSheet(QStringLiteral("color: #7a7a7a; padding-right: 8px;"));
    layout->addWidget(tell_me);
}

void RibbonBar::clear_page() {
    if (page_layout_ == nullptr) {
        return;
    }
    while (QLayoutItem* item = page_layout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Safe here: the only caller is a tab click, and the tab buttons are
            // not children of page_.
            delete widget;
        }
        delete item;
    }
}

void RibbonBar::build_page(int tab_index) {
    const std::vector<TabSpec>& tabs = tab_specs();
    if (tab_index < 0 || static_cast<std::size_t>(tab_index) >= tabs.size()) {
        return;
    }
    active_tab_ = tab_index;
    clear_page();
    button_count_ = 0;

    const TabSpec& tab = tabs[static_cast<std::size_t>(tab_index)];
    for (const GroupSpec& group : tab.groups) {
        auto* box = new QWidget(page_);
        auto* box_layout = new QVBoxLayout(box);
        box_layout->setContentsMargins(4, 0, 8, 0);
        box_layout->setSpacing(1);

        auto* buttons = new QWidget(box);
        auto* row = new QHBoxLayout(buttons);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(2);
        for (const ButtonSpec& spec : group.buttons) {
            auto* button = new QToolButton(buttons);
            button->setObjectName(QStringLiteral("ribbonButton"));
            button->setText(QString::fromUtf8(spec.label));
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setFont(ribbon_font(kRibbonPointSize));
            button->setAutoRaise(true);
            button->setFixedHeight(24);
            button->setFocusPolicy(Qt::NoFocus);
            const app::MenuAction action = spec.action;
            connect(button, &QToolButton::clicked, this, [this, action](bool) {
                if (handler_) {
                    handler_(action);
                }
            });
            row->addWidget(button);
            ++button_count_;
        }
        row->addStretch(1);
        box_layout->addWidget(buttons);

        auto* caption = new QLabel(QString::fromUtf8(group.title), box);
        caption->setObjectName(QStringLiteral("ribbonGroupLabel"));
        caption->setFont(ribbon_font(kGroupLabelPointSize));
        caption->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        caption->setFixedHeight(14);
        box_layout->addWidget(caption);
        page_layout_->addWidget(box);

        auto* separator = new QFrame(page_);
        separator->setFrameShape(QFrame::VLine);
        separator->setFrameShadow(QFrame::Plain);
        separator->setFixedWidth(1);
        separator->setStyleSheet(QStringLiteral("background: #dcdcdc;"));
        page_layout_->addWidget(separator);
    }
    page_layout_->addStretch(1);
}

int RibbonBar::tab_count() const { return static_cast<int>(tab_specs().size()); }

QStringList RibbonBar::tab_titles() const {
    QStringList titles;
    for (const TabSpec& tab : tab_specs()) {
        titles.push_back(QString::fromUtf8(tab.title));
    }
    return titles;
}

QStringList RibbonBar::group_titles() const {
    QStringList titles;
    if (page_layout_ == nullptr) {
        return titles;
    }
    for (int index = 0; index < page_layout_->count(); ++index) {
        QWidget* widget = page_layout_->itemAt(index) ? page_layout_->itemAt(index)->widget()
                                                      : nullptr;
        if (widget == nullptr) {
            continue;
        }
        for (QLabel* label : widget->findChildren<QLabel*>()) {
            if (label->objectName() == QStringLiteral("ribbonGroupLabel")) {
                titles.push_back(label->text());
            }
        }
    }
    return titles;
}

int RibbonBar::button_count() const { return button_count_; }

void RibbonBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kRibbonBackground);
    painter.setPen(QPen(kRibbonBorder, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

}  // namespace msqt
