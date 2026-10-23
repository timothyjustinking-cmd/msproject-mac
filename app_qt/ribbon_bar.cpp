// app_qt/ribbon_bar.cpp -- see ribbon_bar.h.

#include "ribbon_bar.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
#include <QMargins>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "command_status.h"
#include "shell_icons.h"
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

// A ribbon button's stylesheet is `padding: 1px 4px` plus a 1 px transparent
// border, i.e. 5 px on each side of the label.  12 px therefore covers the whole
// style budget and leaves a pixel of slack each side, so a fixed width taken from
// the label's own metrics can never clip it (Qt clips QToolButton text from both
// ends).  It is deliberately not generous: the Task tab must still fit the
// reference's 1920 px with a wide fallback font (DejaVu is ~10 % wider than
// Calibri).
constexpr int kButtonTextPadding = 12;
// The separator after each group is 1 px wide (below).
constexpr int kGroupSeparatorWidth = 1;

}  // namespace

RibbonBar::RibbonBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ribbon"));
    setFixedHeight(kRibbonHeight + 1);
    // Ignored horizontally: the ribbon's natural width is the full 1920-px
    // reference's group row, far wider than the 900-px test window.  Ignoring
    // the size hint keeps the WINDOW at 900x560; groups that do not fit are
    // dropped whole by layout_page_groups().
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMinimumWidth(0);
    setAutoFillBackground(false);

    // One stylesheet: tabs look like tabs, the File tab is the product's dark
    // tile, group captions are small and grey, and the buttons are flat until
    // hovered.
    setStyleSheet(QStringLiteral(
        "QToolButton#ribbonFileTab { background: #1f5c99; color: #ffffff;"
        " border: none; padding: 2px 14px; }"
        "QToolButton#ribbonFileTab:hover { background: #2f6ba8; }"
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

    // The group-row overflow chevron: one ">>" at the right edge instead of
    // wrapping the groups onto a second row.
    overflow_ = new QToolButton(page_);
    overflow_->setObjectName(QStringLiteral("ribbonOverflow"));
    overflow_->setText(QStringLiteral(">>"));
    overflow_->setToolTip(QStringLiteral("More commands"));
    overflow_->setFixedSize(30, 24);
    overflow_->setFocusPolicy(Qt::NoFocus);
    overflow_->hide();
    connect(overflow_, &QToolButton::clicked, this, [this](bool) {
        QMenu menu(this);
        for (const GroupWidgets& group : page_groups_) {
            if (group.box != nullptr && !group.box->isVisible()) {
                QAction* action = menu.addAction(QStringLiteral("More"));
                action->setEnabled(false);
                (void)action;
            }
        }
        menu.exec(mapToGlobal(QPoint(width() - 40, kRibbonTabHeight + 6)));
    });

    tab_overflow_ = new QToolButton(tab_row_);
    tab_overflow_->setObjectName(QStringLiteral("ribbonTabOverflow"));
    tab_overflow_->setText(QStringLiteral(">>"));
    tab_overflow_->setToolTip(QStringLiteral("More tabs"));
    tab_overflow_->setFixedSize(30, kRibbonTabHeight - 4);
    tab_overflow_->setFocusPolicy(Qt::NoFocus);
    tab_overflow_->hide();

    build_tabs();
    build_page(0);
}

RibbonBar::~RibbonBar() = default;

QSize RibbonBar::sizeHint() const { return QSize(900, kRibbonHeight + 1); }

QSize RibbonBar::minimumSizeHint() const { return QSize(320, kRibbonHeight + 1); }

void RibbonBar::set_action_handler(std::function<void(app::MenuAction)> handler) {
    handler_ = std::move(handler);
}

void RibbonBar::set_file_handler(std::function<void()> handler) {
    file_handler_ = std::move(handler);
}

QString RibbonBar::tell_me_text() const {
    return tell_me_ != nullptr ? tell_me_->text() : QString();
}

void RibbonBar::set_contextual_visible(bool visible) {
    contextual_enabled_ = visible;
    update_tab_overflow();
}

void RibbonBar::update_tab_overflow() {
    if (tab_overflow_ == nullptr || tab_row_ == nullptr) {
        return;
    }
    // Widen enough: every tab is shown.  Narrow: the last two tabs go behind the
    // chevron (the strip never wraps to a second row).
    const bool narrow = width() < 1000;
    for (std::size_t index = 0; index < tab_buttons_.size(); ++index) {
        if (tab_buttons_[index] == nullptr) {
            continue;
        }
        const bool hidden_by_context =
            tab_buttons_[index] == contextual_tab_ && !contextual_enabled_;
        const bool hide = hidden_by_context || (narrow && index + 2 >= tab_buttons_.size());
        tab_buttons_[index]->setVisible(!hide);
    }
    if (narrow) {
        tab_overflow_->setVisible(true);
        tab_overflow_->move(tab_row_->width() - tab_overflow_->width() - 8, 2);
        tab_overflow_->raise();
    } else {
        tab_overflow_->hide();
    }
}

void RibbonBar::build_tabs() {
    auto* layout = static_cast<QHBoxLayout*>(tab_row_->layout());
    // The File tab is chrome: dark tile, its own click handler, and deliberately
    // not one of the seven modelled command tabs (tab_count() stays 7).
    file_tab_ = new QToolButton(tab_row_);
    file_tab_->setObjectName(QStringLiteral("ribbonFileTab"));
    file_tab_->setText(QStringLiteral("File"));
    file_tab_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    file_tab_->setFont(ribbon_font(kBodyPointSize - 1));
    file_tab_->setFixedHeight(kRibbonTabHeight - 4);
    file_tab_->setFocusPolicy(Qt::NoFocus);
    connect(file_tab_, &QToolButton::clicked, this, [this](bool) {
        if (file_handler_) {
            file_handler_();
        }
    });
    layout->addWidget(file_tab_);

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
        if (tabs[index].contextual) {
            contextual_tab_ = button;
        }
        tab_buttons_.push_back(button);
        layout->addWidget(button);
    }
    layout->addStretch(1);

    // The reference's right-hand "Tell me" box.  The chrome gate asserts this is
    // present and that its label is the full sentence, not an empty box.
    tell_me_ = new QLabel(QStringLiteral("Tell me what you want to do"), tab_row_);
    tell_me_->setObjectName(QStringLiteral("ribbonTellMe"));
    tell_me_->setFont(ribbon_font(kBodyPointSize - 1));
    tell_me_->setStyleSheet(QStringLiteral("color: #7a7a7a; padding-right: 8px;"));
    layout->addWidget(tell_me_);
}

void RibbonBar::clear_page() {
    page_groups_.clear();
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
    clear_page();
    button_count_ = 0;

    const TabSpec& tab = tabs[static_cast<std::size_t>(tab_index)];
    const QFont button_font = ribbon_font(kRibbonPointSize);
    const QFontMetrics button_metrics(button_font);
    for (const GroupSpec& group : tab.groups) {
        auto* box = new QWidget(page_);
        box->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
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
            const QString label = QString::fromUtf8(spec.label);
            button->setText(label);
            // Icon above label: the product's ribbon button shape.  The icon is
            // drawn by shell_icons.cpp (never Microsoft artwork).
            button->setIcon(shell_action_icon(spec.action, kRibbonIconPx));
            button->setIconSize(QSize(kRibbonIconPx, kRibbonIconPx));
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setFont(button_font);
            button->setAutoRaise(true);
            button->setFixedHeight(kRibbonButtonHeight);
            button->setFocusPolicy(Qt::NoFocus);
            // The width is the LABEL's, from the same font metrics that will
            // draw it, plus the stylesheet's padding/border.  Never narrower
            // than the style's own hint.  This is what stops "Cut"/"Copy"/
            // "Format Painter" from being clipped to "Cu"/"op"/"t F".
            const int label_width =
                button_metrics.horizontalAdvance(label) + kButtonTextPadding;
            button->setFixedWidth(std::max(label_width, button->sizeHint().width()));
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            const app::MenuAction action = spec.action;
            // Decision 9: an unimplemented ribbon command is greyed with the
            // exact tooltip, never silently dead.
            const bool implemented = shell_implements(action);
            button->setEnabled(implemented);
            button->setToolTip(implemented ? label
                                           : QStringLiteral("not implemented in this port"));
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
        caption->setFixedHeight(kRibbonGroupLabelHeight);
        box_layout->addWidget(caption);

        // The group's width follows its widest child (the button row or the
        // caption); the box layout's minimum already reports that, so a narrower
        // page cannot squeeze the buttons below their labels.
        box->setMinimumWidth(box->sizeHint().width());
        page_layout_->addWidget(box);

        auto* separator = new QFrame(page_);
        separator->setFrameShape(QFrame::VLine);
        separator->setFrameShadow(QFrame::Plain);
        separator->setFixedWidth(kGroupSeparatorWidth);
        separator->setStyleSheet(QStringLiteral("background: #dcdcdc;"));
        page_layout_->addWidget(separator);

        GroupWidgets widgets;
        widgets.box = box;
        widgets.separator = separator;
        page_groups_.push_back(widgets);
    }
    page_layout_->addStretch(1);
    layout_page_groups();
}

void RibbonBar::layout_page_groups() {
    if (page_layout_ == nullptr) {
        return;
    }
    const QMargins margins = page_layout_->contentsMargins();
    const int available = width() - margins.left() - margins.right();
    if (available < 200) {
        // The ribbon has not been laid out yet (construction).  Show everything
        // and let the first resize settle it; hiding here would hide the whole
        // page at width 0.
        for (GroupWidgets& group : page_groups_) {
            if (group.box != nullptr) {
                group.box->setVisible(true);
            }
            if (group.separator != nullptr) {
                group.separator->setVisible(true);
            }
        }
        return;
    }
    int used = 0;
    bool overflow = false;
    for (GroupWidgets& group : page_groups_) {
        if (group.box == nullptr || group.separator == nullptr) {
            continue;
        }
        const int wanted = group.box->sizeHint().width() + kGroupSeparatorWidth;
        const bool fits = !overflow && used + wanted <= available;
        group.box->setVisible(fits);
        group.separator->setVisible(fits);
        if (fits) {
            used += wanted;
        } else {
            // Ribbon order is preserved: once a group does not fit, nothing
            // after it is shown either.
            overflow = true;
        }
    }
    groups_overflow_ = overflow;
    if (overflow_ != nullptr) {
        // One chevron at the group row's right edge; the row height is unchanged
        // (no wrapping).
        overflow_->setVisible(overflow);
        if (overflow) {
            overflow_->move(width() - overflow_->width() - 6, 8);
            overflow_->raise();
        }
    }
    page_layout_->invalidate();
}

int RibbonBar::visible_group_count() const {
    int count = 0;
    for (const GroupWidgets& group : page_groups_) {
        if (group.box != nullptr && group.box->isVisible()) {
            ++count;
        }
    }
    return count;
}

void RibbonBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layout_page_groups();
    update_tab_overflow();
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
