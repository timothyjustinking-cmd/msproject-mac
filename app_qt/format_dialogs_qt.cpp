// app_qt/format_dialogs_qt.cpp -- see format_dialogs_qt.h.

#include "format_dialogs_qt.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace msqt {
namespace {

using namespace format_dialog_geometry;

const QString kDisabledTip = QString::fromUtf8(kNotImplemented);

void place(QWidget* widget, int x, int y, int width, int height) {
    widget->setGeometry(x, y, width, height);
}

QLabel* make_label(QWidget* parent, const QString& text, int x, int y, int width,
                   int height, bool word_wrap = false) {
    auto* label = new QLabel(text, parent);
    place(label, x, y, width, height);
    label->setWordWrap(word_wrap);
    return label;
}

QComboBox* make_combo(QWidget* parent, int x, int y, int width, int height,
                      const QStringList& items, bool enabled) {
    auto* combo = new QComboBox(parent);
    place(combo, x, y, width, height);
    combo->addItems(items);
    if (!enabled) {
        combo->setEnabled(false);
        combo->setToolTip(kDisabledTip);
    }
    return combo;
}

QPushButton* make_button(QWidget* parent, const QString& text, int x, int y, int width,
                         int height, bool enabled) {
    auto* button = new QPushButton(text, parent);
    place(button, x, y, width, height);
    if (!enabled) {
        button->setEnabled(false);
        button->setToolTip(kDisabledTip);
    }
    return button;
}

QString colour_text(const msproject::viewstate::Rgb& colour) {
    return QStringLiteral("RGB(%1, %2, %3)")
        .arg(colour.red)
        .arg(colour.green)
        .arg(colour.blue);
}

const std::array<msproject::viewstate::Rgb, 15>& palette() {
    using msproject::viewstate::Rgb;
    static const std::array<Rgb, 15> kPalette = {
        Rgb{0, 0, 0},       Rgb{255, 255, 255}, Rgb{128, 0, 0},
        Rgb{0, 128, 0},     Rgb{0, 0, 128},     Rgb{128, 128, 0},
        Rgb{0, 128, 128},   Rgb{128, 0, 128},   Rgb{255, 0, 0},
        Rgb{0, 255, 0},     Rgb{0, 0, 255},     Rgb{255, 255, 0},
        Rgb{128, 128, 128}, Rgb{192, 192, 192}, Rgb{255, 165, 0},
    };
    return kPalette;
}

// Fills a colour combo with "Automatic" followed by the port's palette; an
// explicit current colour that is not in the palette is inserted after
// Automatic so the dialog always shows the file's own value.
void fill_colour_combo(QComboBox* combo, bool has_colour,
                       const msproject::viewstate::Rgb& colour) {
    combo->addItem(QStringLiteral("Automatic"),
                   QVariant::fromValue<std::uint32_t>(0xffffffffu));
    const QString current = has_colour ? colour_text(colour) : QString();
    if (has_colour && current != QStringLiteral("RGB(0, 0, 0)")) {
        bool found = false;
        for (const msproject::viewstate::Rgb& entry : palette()) {
            if (entry == colour) {
                found = true;
                break;
            }
        }
        if (!found) {
            const std::uint32_t key = (static_cast<std::uint32_t>(colour.red) << 16u) |
                                      (static_cast<std::uint32_t>(colour.green) << 8u) |
                                      static_cast<std::uint32_t>(colour.blue);
            combo->addItem(current, QVariant::fromValue<std::uint32_t>(key));
        }
    }
    for (const msproject::viewstate::Rgb& entry : palette()) {
        const std::uint32_t key = (static_cast<std::uint32_t>(entry.red) << 16u) |
                                  (static_cast<std::uint32_t>(entry.green) << 8u) |
                                  static_cast<std::uint32_t>(entry.blue);
        combo->addItem(colour_text(entry), QVariant::fromValue<std::uint32_t>(key));
    }
    if (has_colour) {
        const int index = combo->findText(current);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    } else {
        combo->setCurrentIndex(0);
    }
}

std::uint32_t combo_colour(const QComboBox* combo, bool& has_colour) {
    const std::uint32_t key = combo->currentData().toUInt();
    if (key == 0xffffffffu) {
        has_colour = false;
        return 0;
    }
    has_colour = true;
    return key;
}

// Called with the current combo index whenever a functional colour changes.
void apply_rgb(std::uint32_t key, bool& has_colour, msproject::viewstate::Rgb& out) {
    if (key == 0xffffffffu) {
        has_colour = false;
        out = msproject::viewstate::Rgb{};
        return;
    }
    has_colour = true;
    out.red = static_cast<std::uint8_t>((key >> 16u) & 0xffu);
    out.green = static_cast<std::uint8_t>((key >> 8u) & 0xffu);
    out.blue = static_cast<std::uint8_t>(key & 0xffu);
}

// The Text Styles item combo's order is Project's (the wave-29 capture).  Each
// row maps to the style record's slot; row 0 is All.  Project Summary has no
// row of its own, and All is the only way to reach it, which is why All applies
// to every one of the 19 stored styles.
constexpr int kItemTarget[19] = {
    -1,  // All
    1,   // Row & Column Titles
    3,   // Critical Tasks
    15,  // External Tasks
    0,   // Highlighted Tasks
    13,  // Marked Tasks
    2,   // Noncritical Tasks
    5,   // Milestone Tasks
    4,   // Summary Tasks
    16,  // Top Timescale Tier
    6,   // Middle Timescale Tier
    7,   // Bottom Timescale Tier
    8,   // Bar Text - Left
    9,   // Bar Text - Right
    10,  // Bar Text - Top
    11,  // Bar Text - Bottom
    12,  // Bar Text - Inside
    17,  // Changed Cells
    18,  // Inactive Tasks
};

QStringList text_style_items() {
    return {
        QStringLiteral("All"),
        QStringLiteral("Row & Column Titles"),
        QStringLiteral("Critical Tasks"),
        QStringLiteral("External Tasks"),
        QStringLiteral("Highlighted Tasks"),
        QStringLiteral("Marked Tasks"),
        QStringLiteral("Noncritical Tasks"),
        QStringLiteral("Milestone Tasks"),
        QStringLiteral("Summary Tasks"),
        QStringLiteral("Top Timescale Tier"),
        QStringLiteral("Middle Timescale Tier"),
        QStringLiteral("Bottom Timescale Tier"),
        QStringLiteral("Bar Text - Left"),
        QStringLiteral("Bar Text - Right"),
        QStringLiteral("Bar Text - Top"),
        QStringLiteral("Bar Text - Bottom"),
        QStringLiteral("Bar Text - Inside"),
        QStringLiteral("Changed Cells"),
        QStringLiteral("Inactive Tasks"),
    };
}

QStringList size_items() {
    return {QStringLiteral("8"),  QStringLiteral("9"),  QStringLiteral("10"),
            QStringLiteral("11"), QStringLiteral("12"), QStringLiteral("14"),
            QStringLiteral("16"), QStringLiteral("18"), QStringLiteral("20"),
            QStringLiteral("22"), QStringLiteral("24"), QStringLiteral("26"),
            QStringLiteral("28"), QStringLiteral("36"), QStringLiteral("48"),
            QStringLiteral("72")};
}

QStringList background_patterns() {
    return {
        QStringLiteral("Hollow"),
        QStringLiteral("Solid Fill"),
        QStringLiteral("Light Fill"),
        QStringLiteral("Medium Fill"),
        QStringLiteral("Dark Fill"),
        QStringLiteral("Diagonal Left"),
        QStringLiteral("Diagonal Right"),
        QStringLiteral("Dark Diagonal Left"),
        QStringLiteral("Dark Diagonal Right"),
        QStringLiteral("Thin Vertical Stripe"),
        QStringLiteral("Medium Vertical Stripe"),
        QStringLiteral("Light Dither"),
        QStringLiteral("Medium Dither"),
        QStringLiteral("Dark Dither"),
    };
}

// A bar colour combo: Automatic (0xffffffff) then the palette, each item
// carrying its 0xRRGGBB key so the edit reads back the colour it shows.
void fill_bar_colour_combo(QComboBox* combo) {
    combo->addItem(QStringLiteral("Automatic"),
                   QVariant::fromValue<std::uint32_t>(0xffffffffu));
    for (const msproject::viewstate::Rgb& entry : palette()) {
        const std::uint32_t key = (static_cast<std::uint32_t>(entry.red) << 16u) |
                                  (static_cast<std::uint32_t>(entry.green) << 8u) |
                                  static_cast<std::uint32_t>(entry.blue);
        combo->addItem(colour_text(entry), QVariant::fromValue<std::uint32_t>(key));
    }
}

// Shows a text style's colour even when it is outside the port's palette: the
// file's own colour is appended once, so a stored non-palette value (Changed
// Cells' pale-blue background is the stock case) is shown, and if the user
// changes another field the untouched colour still reads back as itself.
int colour_index(bool has_colour, const msproject::viewstate::Rgb& colour,
                 QComboBox* combo) {
    if (!has_colour) {
        return 0;
    }
    const QString text = colour_text(colour);
    int index = combo->findText(text);
    if (index < 0) {
        const std::uint32_t key = (static_cast<std::uint32_t>(colour.red) << 16u) |
                                  (static_cast<std::uint32_t>(colour.green) << 8u) |
                                  static_cast<std::uint32_t>(colour.blue);
        combo->addItem(text, QVariant::fromValue<std::uint32_t>(key));
        index = combo->count() - 1;
    }
    return index;
}

// Shows a bar colour even when it is outside the port's palette: the file's own
// colour is appended once, so an untouched combo keeps the stored value and a
// changed combo stores the colour it shows.
void select_bar_colour(QComboBox* combo, bool has_colour,
                       const msproject::viewstate::Rgb& colour) {
    if (!has_colour) {
        combo->setCurrentIndex(0);
        return;
    }
    const QString text = colour_text(colour);
    int index = combo->findText(text);
    if (index < 0) {
        const std::uint32_t key = (static_cast<std::uint32_t>(colour.red) << 16u) |
                                  (static_cast<std::uint32_t>(colour.green) << 8u) |
                                  static_cast<std::uint32_t>(colour.blue);
        combo->addItem(text, QVariant::fromValue<std::uint32_t>(key));
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

struct BarFieldChoice {
    std::uint32_t id;
    const char* name;
};

const BarFieldChoice kBarTextFields[] = {
    {0xffffffffu, "None"},
    {0x0b40000eu, "Name"},
    {0x0b400017u, "ID"},
    {0x0b400020u, "% Complete"},
    {0x0b400031u, "Resource Names"},
    {0x0b400503u, "Start"},
    {0x0b400504u, "Finish"},
    {0x0b400505u, "Start Text"},
    {0x0b400506u, "Finish Text"},
    {0x0b400507u, "Duration"},
    {0x0b400508u, "Duration Text"},
};

QStringList bar_text_field_items() {
    QStringList items;
    for (const BarFieldChoice& choice : kBarTextFields) {
        items << QString::fromLatin1(choice.name);
    }
    return items;
}

void fill_bar_field_combo(QComboBox* combo) {
    for (const BarFieldChoice& choice : kBarTextFields) {
        combo->addItem(QString::fromLatin1(choice.name),
                       QVariant::fromValue<std::uint32_t>(choice.id));
    }
}

void select_bar_field(QComboBox* combo, std::uint32_t field) {
    int index = combo->findData(QVariant::fromValue<std::uint32_t>(field));
    if (index < 0) {
        combo->addItem(QStringLiteral("0x%1").arg(field, 8, 16, QLatin1Char('0')),
                       QVariant::fromValue<std::uint32_t>(field));
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

std::uint32_t selected_bar_field(const QComboBox* combo) {
    return combo->currentData().toUInt();
}

// The From/To column's field names.  The wave-29 capture shows these labels
// for the stock rows (Task Start/Task Finish, Complete Through, Deliverable
// Start/Finish, Deadline); ids outside that measured set print as hex.
QString from_to_field_text(std::uint32_t field, bool from_side) {
    switch (field) {
        case 0u:
            return from_side ? QStringLiteral("Project Start")
                             : QStringLiteral("Project Finish");
        case 0x0b400503u: return QStringLiteral("Task Start");
        case 0x0b400504u: return QStringLiteral("Task Finish");
        case 0x0b400077u: return QStringLiteral("Complete Through");
        case 0x0b400480u: return QStringLiteral("Deliverable Start");
        case 0x0b400481u: return QStringLiteral("Deliverable Finish");
        case 0x0b4001b5u: return QStringLiteral("Deadline");
        case 0x0b400029u: return QStringLiteral("Start");
        case 0x0b400023u: return QStringLiteral("Start");
        case 0x0b400024u: return QStringLiteral("Finish");
        default:
            return QStringLiteral("0x%1").arg(field, 8, 16, QLatin1Char('0'));
    }
}

// The Show For ... Tasks text: every show bit's category name in ascending
// order, then every "Not ..." bit's category name.  Ascending order matches the
// capture for Task (Normal, Active, Not Manually Scheduled).
QString show_for_text(std::uint64_t show_for, std::uint64_t show_for_not) {
    struct Category {
        int bit;
        const char* name;
    };
    static const Category kCategories[] = {
        {0, "Normal"},          {1, "Milestone"},        {2, "Summary"},
        {3, "Critical"},        {4, "Noncritical"},      {6, "Finished"},
        {7, "In Progress"},     {26, "Rolled Up"},       {27, "Project Summary"},
        {28, "Split"},          {40, "Group By Summary"}, {41, "Deliverable"},
        {42, "Dependency"},     {43, "Active"},          {44, "Manually Scheduled"},
        {45, "Warning"},        {49, "Placeholder"},
    };
    QStringList parts;
    for (const Category& category : kCategories) {
        if (((show_for >> category.bit) & 1u) != 0) {
            parts << QString::fromLatin1(category.name);
        }
    }
    for (const Category& category : kCategories) {
        if (((show_for_not >> category.bit) & 1u) != 0) {
            parts << QStringLiteral("Not ") + QString::fromLatin1(category.name);
        }
    }
    return parts.join(QStringLiteral(", "));
}

QIcon bar_appearance_icon(const msproject::viewstate::BarStyle& bar) {
    QPixmap pixmap(64, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const auto colour_of = [](bool has, const msproject::viewstate::Rgb& colour) {
        return has ? QColor(colour.red, colour.green, colour.blue)
                   : QColor(0x8a, 0xbb, 0xed);
    };
    const QColor middle = colour_of(bar.has_middle_colour, bar.middle_colour);
    if (bar.middle_shape != 0) {
        const QRect body(16, 5, 32, 7);
        painter.setPen(QPen(middle, 1));
        if (bar.middle_pattern == 0) {
            painter.drawRect(body);
        } else {
            painter.fillRect(body, middle);
        }
    }
    const auto marker = [&painter](std::uint8_t shape, const QColor& colour, int x) {
        if (shape == 0) {
            return;
        }
        painter.setPen(QPen(colour, 1));
        painter.setBrush(colour);
        switch (shape) {
            case 3: {  // diamond: the Milestone shape
                const QPoint points[4] = {QPoint(x, 2), QPoint(x + 4, 8), QPoint(x, 14),
                                          QPoint(x - 4, 8)};
                painter.drawPolygon(points, 4);
                break;
            }
            case 19:  // circle
                painter.drawEllipse(QPoint(x, 8), 4, 4);
                break;
            case 18:  // square
                painter.drawRect(QRect(x - 4, 4, 8, 8));
                break;
            default:  // any other measured shape: a small solid tick
                painter.drawRect(QRect(x - 2, 6, 4, 5));
                break;
        }
    };
    marker(bar.start_shape, colour_of(bar.has_start_colour, bar.start_colour), 10);
    marker(bar.end_shape, colour_of(bar.has_end_colour, bar.end_colour), 54);
    painter.end();
    return QIcon(pixmap);
}

}  // namespace

// ---------------------------------------------------------------------------
// Text Styles
// ---------------------------------------------------------------------------
TextStylesDialog::TextStylesDialog(const GanttStyle& style, QWidget* parent)
    : QDialog(parent), source_(style), working_(style), result_(style) {
    setWindowTitle(QStringLiteral("Text Styles"));
    setFixedSize(kTextStylesWidth, kTextStylesHeight);

    make_label(this, QStringLiteral("&Item to Change:"), 6, 10, 79, 13);
    item_ = make_combo(this, 94, 7, 152, 21, text_style_items(), true);

    make_label(this, QStringLiteral("&Font:"), 6, 31, 26, 13);
    font_ = make_combo(this, 6, 47, 154, 21, QStringList(), true);
    QStringList families;
    for (const msproject::viewstate::FontBase& base : source_.fonts) {
        if (!base.name.empty()) {
            families << QString::fromStdString(base.name);
        }
    }
    for (const char* name : {"Calibri", "Arial", "Times New Roman", "Tahoma",
                             "Verdana", "Segoe UI", "Courier New", "Georgia"}) {
        families << QString::fromLatin1(name);
    }
    families.removeDuplicates();
    families.sort(Qt::CaseInsensitive);
    font_->addItems(families);

    make_label(this, QStringLiteral("Font st&yle:"), 169, 31, 52, 13);
    font_style_ = make_combo(this, 169, 47, 78, 21,
                             {QStringLiteral("Regular"), QStringLiteral("Italic"),
                              QStringLiteral("Bold"), QStringLiteral("Bold Italic")},
                             true);

    make_label(this, QStringLiteral("&Size:"), 256, 31, 23, 13);
    size_ = make_combo(this, 256, 47, 62, 21, size_items(), true);

    ok_ = make_button(this, QStringLiteral("OK"), 327, 7, 75, 23, true);
    ok_->setObjectName(QStringLiteral("okButton"));
    cancel_ = make_button(this, QStringLiteral("Cancel"), 327, 37, 75, 23, true);
    cancel_->setObjectName(QStringLiteral("cancelButton"));

    underline_ = new QCheckBox(QStringLiteral("&Underline"), this);
    place(underline_, 6, 159, 65, 16);
    strikethrough_ = new QCheckBox(QStringLiteral("S&trikethrough"), this);
    place(strikethrough_, 80, 159, 85, 16);

    make_label(this, QStringLiteral("&Color:"), 6, 181, 29, 13);
    colour_ = make_combo(this, 6, 197, 152, 20, QStringList(), true);

    auto* sample_box = new QGroupBox(QStringLiteral("Sample"), this);
    place(sample_box, 167, 181, 235, 92);
    sample_ = new QLabel(QStringLiteral("AaBbYyZz"), sample_box);
    place(sample_, 9, 20, 217, 65);
    sample_->setAlignment(Qt::AlignCenter);

    make_label(this, QStringLiteral("&Background Color:"), 6, 220, 88, 13);
    background_ = make_combo(this, 6, 236, 152, 20, QStringList(), true);

    make_label(this, QStringLiteral("Background &Pattern:"), 6, 259, 99, 13);
    pattern_ = make_combo(this, 6, 275, 152, 20, background_patterns(), false);
    pattern_->setToolTip(kDisabledTip);

    note_ = make_label(
        this,
        QStringLiteral("Applies to this view for the session. A saved style is a "
                       "write path with no Project pair in this port, so Save keeps "
                       "the file's bytes. Background patterns are not implemented "
                       "in this port."),
        6, 306, 396, 39, true);

    fill_colour_combo(colour_, false, msproject::viewstate::Rgb{});
    fill_colour_combo(background_, false, msproject::viewstate::Rgb{});

    if (!source_.decoded) {
        for (QWidget* control :
             {static_cast<QWidget*>(item_), static_cast<QWidget*>(font_),
              static_cast<QWidget*>(font_style_), static_cast<QWidget*>(size_),
              static_cast<QWidget*>(underline_), static_cast<QWidget*>(strikethrough_),
              static_cast<QWidget*>(colour_), static_cast<QWidget*>(background_),
              static_cast<QWidget*>(pattern_), static_cast<QWidget*>(sample_box)}) {
            control->setEnabled(false);
            control->setToolTip(kDisabledTip);
        }
        ok_->setEnabled(false);
        ok_->setToolTip(kDisabledTip);
        note_->setText(QStringLiteral(
            "No Gantt style record in this project; there is nothing to apply."));
    }

    connect(item_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) { load_item(index); });
    connect(font_, &QComboBox::currentTextChanged, this,
            [this](const QString&) { apply_field(Field::family); });
    connect(font_style_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { apply_field(Field::font_style); });
    connect(size_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { apply_field(Field::size); });
    connect(underline_, &QCheckBox::toggled, this,
            [this](bool) { apply_field(Field::underline); });
    connect(strikethrough_, &QCheckBox::toggled, this,
            [this](bool) { apply_field(Field::strikethrough); });
    connect(colour_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { apply_field(Field::colour); });
    connect(background_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { apply_field(Field::background); });
    connect(ok_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);

    load_item(0);
}

std::vector<std::size_t> TextStylesDialog::targets() const {
    std::vector<std::size_t> out;
    const int item = item_->currentIndex();
    if (item <= 0) {
        for (std::size_t index = 0; index < working_.text.size(); ++index) {
            out.push_back(index);
        }
        return out;
    }
    if (item < static_cast<int>(std::size(kItemTarget))) {
        out.push_back(static_cast<std::size_t>(kItemTarget[item]));
    }
    return out;
}

void TextStylesDialog::load_item(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= std::size(kItemTarget)) {
        return;
    }
    loading_ = true;
    int slot = kItemTarget[index];
    if (slot < 0) {
        // All: the controls show the first stored style.  A field the user then
        // changes is written to every style; untouched fields of the other
        // styles keep their own values.
        slot = 0;
    }
    const msproject::viewstate::TextStyle& style =
        working_.text[static_cast<std::size_t>(slot)];
    if (!style.font_name.empty()) {
        int family = font_->findText(QString::fromStdString(style.font_name));
        if (family < 0) {
            font_->addItem(QString::fromStdString(style.font_name));
            family = font_->count() - 1;
        }
        font_->setCurrentIndex(family);
    }
    font_style_->setCurrentIndex((style.bold ? 2 : 0) + (style.italic ? 1 : 0));
    int size = size_->findText(QString::number(style.point_size));
    if (size < 0 && style.point_size > 0) {
        size_->addItem(QString::number(style.point_size));
        size = size_->count() - 1;
    }
    size_->setCurrentIndex(size >= 0 ? size : 0);
    underline_->setChecked(style.underline);
    strikethrough_->setChecked(style.strikethrough);
    colour_->setCurrentIndex(colour_index(style.has_colour, style.colour, colour_));
    background_->setCurrentIndex(
        colour_index(style.has_background, style.background, background_));
    pattern_->setCurrentIndex(
        std::min<int>(style.background_pattern, pattern_->count() - 1));
    loading_ = false;
    refresh_sample();
}

void TextStylesDialog::apply_field(Field field) {
    if (loading_ || !source_.decoded) {
        return;
    }
    bool any = false;
    for (const std::size_t index : targets()) {
        if (index >= working_.text.size()) {
            continue;
        }
        msproject::viewstate::TextStyle& style = working_.text[index];
        switch (field) {
            case Field::family:
                style.font_name = font_->currentText().toStdString();
                style.valid = !style.font_name.empty();
                break;
            case Field::font_style:
                style.bold = font_style_->currentIndex() >= 2;
                style.italic = (font_style_->currentIndex() % 2) != 0;
                break;
            case Field::size:
                style.point_size = size_->currentText().toInt();
                break;
            case Field::underline:
                style.underline = underline_->isChecked();
                break;
            case Field::strikethrough:
                style.strikethrough = strikethrough_->isChecked();
                break;
            case Field::colour: {
                bool has = false;
                std::uint32_t key = combo_colour(colour_, has);
                apply_rgb(key, has, style.colour);
                style.has_colour = has;
                break;
            }
            case Field::background: {
                bool has = false;
                std::uint32_t key = combo_colour(background_, has);
                apply_rgb(key, has, style.background);
                style.has_background = has;
                break;
            }
        }
        any = true;
    }
    if (any) {
        changed_ = true;
        refresh_sample();
    }
}

void TextStylesDialog::refresh_sample() {
    QFont font = sample_->font();
    if (!font_->currentText().isEmpty()) {
        font.setFamily(font_->currentText());
    }
    const int size = size_->currentText().toInt();
    if (size > 0) {
        font.setPointSize(size);
    }
    font.setBold(font_style_->currentIndex() >= 2);
    font.setItalic((font_style_->currentIndex() % 2) != 0);
    font.setUnderline(underline_->isChecked());
    font.setStrikeOut(strikethrough_->isChecked());
    sample_->setFont(font);
    QString css;
    QString text_colour = QStringLiteral("#000000");
    if (colour_->currentData().toUInt() != 0xffffffffu) {
        const std::uint32_t key = colour_->currentData().toUInt();
        text_colour = QStringLiteral("#%1")
                          .arg(key & 0xffffffu, 6, 16, QLatin1Char('0'));
    }
    css += QStringLiteral("color: %1;").arg(text_colour);
    if (background_->currentData().toUInt() != 0xffffffffu) {
        const std::uint32_t key = background_->currentData().toUInt();
        css += QStringLiteral(" background-color: #%1;")
                   .arg(key & 0xffffffu, 6, 16, QLatin1Char('0'));
    }
    sample_->setStyleSheet(css);
}

void TextStylesDialog::select_item(int index) {
    if (index >= 0 && index < item_->count()) {
        item_->setCurrentIndex(index);
    }
}

void TextStylesDialog::accept() {
    if (!source_.decoded) {
        return;
    }
    result_ = working_;
    QDialog::accept();
}

// ---------------------------------------------------------------------------
// Gridlines
// ---------------------------------------------------------------------------
GridlinesDialog::GridlinesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Gridlines"));
    setFixedSize(kGridlinesWidth, kGridlinesHeight);

    make_label(this, QStringLiteral("&Line to change:"), 6, 7, 79, 13);
    list_ = new QListWidget(this);
    place(list_, 8, 25, 139, 145);
    list_->addItems({QStringLiteral("Gantt Rows"), QStringLiteral("Bar Rows"),
                     QStringLiteral("Middle Tier Column"),
                     QStringLiteral("Bottom Tier Column"),
                     QStringLiteral("Current Date"), QStringLiteral("Sheet Rows"),
                     QStringLiteral("Sheet Columns"), QStringLiteral("Title Vertical"),
                     QStringLiteral("Title Horizontal"), QStringLiteral("Page Breaks"),
                     QStringLiteral("Project Start"), QStringLiteral("Project Finish"),
                     QStringLiteral("Status Date"), QStringLiteral("Top Tier Column")});
    list_->setCurrentRow(0);
    list_->setEnabled(false);
    list_->setToolTip(kDisabledTip);

    auto* normal = new QGroupBox(QStringLiteral("Normal"), this);
    place(normal, 159, 7, 307, 48);
    auto* normal_type_label = new QLabel(QStringLiteral("&Type:"), normal);
    place(normal_type_label, 9, 20, 25, 21);
    normal_type_ = make_combo(
        normal, 43, 20, 103, 21,
        {QStringLiteral("No Line"), QStringLiteral("Continuous"), QStringLiteral("Dash"),
         QStringLiteral("Dot"), QStringLiteral("Dash Dot")},
        false);
    auto* normal_colour_label = new QLabel(QStringLiteral("&Color:"), normal);
    place(normal_colour_label, 155, 20, 31, 21);
    normal_colour_ = make_combo(normal, 195, 20, 103, 21,
                                {QStringLiteral("Automatic")}, false);

    auto* interval = new QGroupBox(QStringLiteral("At interval"), this);
    place(interval, 159, 58, 307, 89);
    auto* group = new QButtonGroup(this);
    none_ = new QRadioButton(QStringLiteral("&None"), interval);
    place(none_, 9, 22, 48, 16);
    none_->setChecked(true);
    interval_2_ = new QRadioButton(QStringLiteral("&2"), interval);
    place(interval_2_, 66, 22, 26, 16);
    interval_3_ = new QRadioButton(QStringLiteral("&3"), interval);
    place(interval_3_, 101, 22, 26, 16);
    interval_4_ = new QRadioButton(QStringLiteral("&4"), interval);
    place(interval_4_, 136, 22, 26, 16);
    other_ = new QRadioButton(QStringLiteral("&Other:"), interval);
    place(other_, 171, 22, 53, 16);
    other_spin_ = new QSpinBox(interval);
    place(other_spin_, 232, 19, 50, 22);
    other_spin_->setRange(1, 999);
    other_spin_->setValue(1);
    for (QRadioButton* radio : {none_, interval_2_, interval_3_, interval_4_, other_}) {
        group->addButton(radio);
        radio->setEnabled(false);
        radio->setToolTip(kDisabledTip);
    }
    other_spin_->setEnabled(false);
    other_spin_->setToolTip(kDisabledTip);

    note_ = new QLabel(
        QStringLiteral("Gridlines are not implemented in this port; OK is disabled."),
        interval);
    place(note_, 9, 42, 287, 16);
    QFont note_font = note_->font();
    note_font.setPointSize(7);
    note_->setFont(note_font);

    auto* interval_type_label = new QLabel(QStringLiteral("T&ype:"), interval);
    place(interval_type_label, 9, 61, 25, 21);
    interval_type_ = make_combo(
        interval, 43, 61, 103, 21,
        {QStringLiteral("No Line"), QStringLiteral("Continuous"), QStringLiteral("Dash"),
         QStringLiteral("Dot"), QStringLiteral("Dash Dot")},
        false);
    auto* interval_colour_label = new QLabel(QStringLiteral("Colo&r:"), interval);
    place(interval_colour_label, 155, 61, 31, 21);
    interval_colour_ = make_combo(interval, 195, 61, 103, 21,
                                  {QStringLiteral("Automatic")}, false);

    help_ = make_button(this, QStringLiteral("&Help"), 159, 158, 75, 23, false);
    ok_ = make_button(this, QStringLiteral("OK"), 307, 158, 75, 23, false);
    ok_->setObjectName(QStringLiteral("okButton"));
    ok_->setToolTip(kDisabledTip);
    cancel_ = make_button(this, QStringLiteral("Cancel"), 391, 158, 75, 23, true);
    cancel_->setObjectName(QStringLiteral("cancelButton"));

    connect(ok_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
}

// ---------------------------------------------------------------------------
// Bar Styles
// ---------------------------------------------------------------------------
BarStylesDialog::BarStylesDialog(const GanttStyle& style, QWidget* parent)
    : QDialog(parent), source_(style), working_(style), result_(style) {
    setWindowTitle(QStringLiteral("Bar Styles"));
    setFixedSize(kBarStylesWidth, kBarStylesHeight);

    cut_ = make_button(this, QStringLiteral("Cut Row"), 14, 11, 75, 23, false);
    paste_ = make_button(this, QStringLiteral("Paste Row"), 98, 11, 75, 23, false);
    insert_ = make_button(this, QStringLiteral("Insert Row"), 182, 11, 75, 23, false);

    table_ = new QTableWidget(this);
    place(table_, 6, 41, 735, 290);
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Appearance"),
         QStringLiteral("Show For ... Tasks"), QStringLiteral("Row"),
         QStringLiteral("From"), QStringLiteral("To")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->setColumnWidth(0, 160);
    table_->setColumnWidth(1, 90);
    table_->setColumnWidth(2, 255);
    table_->setColumnWidth(3, 45);
    table_->setColumnWidth(4, 85);
    table_->setColumnWidth(5, 100);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);

    tabs_ = new QTabWidget(this);
    place(tabs_, 6, 339, 735, 151);

    auto* text_page = new QWidget(tabs_);
    auto* text_layout = new QVBoxLayout(text_page);
    text_layout->setContentsMargins(9, 0, 9, 0);
    text_layout->setSpacing(0);
    text_table_ = new QTableWidget(text_page);
    text_table_->setColumnCount(2);
    text_table_->setHorizontalHeaderLabels({QString(), QString()});
    text_table_->verticalHeader()->setVisible(false);
    text_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    text_table_->setColumnWidth(0, 145);
    text_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    text_table_->setRowCount(5);
    text_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    text_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    text_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    const char* const sides[5] = {"Left", "Right", "Top", "Bottom", "Inside"};
    for (int row = 0; row < 5; ++row) {
        auto* item = new QTableWidgetItem(QString::fromLatin1(sides[row]));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QFont bold = item->font();
        bold.setBold(true);
        item->setFont(bold);
        text_table_->setItem(row, 0, item);
        text_table_->setRowHeight(row, 18);
    }
    text_layout->addWidget(text_table_);
    tabs_->addTab(text_page, QStringLiteral("Text"));

    QComboBox** field_combos[5] = {&left_field_, &right_field_, &top_field_,
                                   &bottom_field_, &inside_field_};
    for (int row = 0; row < 5; ++row) {
        auto* combo = new QComboBox(text_table_);
        fill_bar_field_combo(combo);
        text_table_->setCellWidget(row, 1, combo);
        *field_combos[row] = combo;
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, row](int) { apply_text_field(row); });
    }

    auto* bars_page = new QWidget(tabs_);
    const auto add_group = [bars_page](const QString& title, int x, int width) {
        auto* group = new QGroupBox(title, bars_page);
        place(group, x, 8, width, 96);
        return group;
    };
    const auto add_field_label = [](QGroupBox* group, const QString& text, int x,
                                    int y) {
        auto* label = new QLabel(text, group);
        place(label, x, y, 44, 13);
        return label;
    };
    auto* start_group = add_group(QStringLiteral("Start"), 17, 226);
    add_field_label(start_group, QStringLiteral("Sh&ape:"), 9, 24);
    start_shape_ = make_combo(start_group, 53, 20, 164, 21,
                              {QStringLiteral("No Shape"), QStringLiteral("House Up"),
                               QStringLiteral("House Down"), QStringLiteral("Diamond"),
                               QStringLiteral("Circle Diamond"),
                               QStringLiteral("Triangle Up"),
                               QStringLiteral("Triangle Down"),
                               QStringLiteral("Triangle Right"),
                               QStringLiteral("Triangle Left"),
                               QStringLiteral("Circle Triangle Up"),
                               QStringLiteral("Circle Triangle Down"),
                               QStringLiteral("Arrow Up"), QStringLiteral("Arrow Down"),
                               QStringLiteral("Circle Arrow Up"),
                               QStringLiteral("Circle Arrow Down"),
                               QStringLiteral("Caret Down Top"),
                               QStringLiteral("Caret Up Bottom"),
                               QStringLiteral("Line Shape"), QStringLiteral("Square"),
                               QStringLiteral("Circle"), QStringLiteral("Star"),
                               QStringLiteral("Left Brace"), QStringLiteral("Right Brace"),
                               QStringLiteral("Fade Left"), QStringLiteral("Fade right")},
                              false);
    add_field_label(start_group, QStringLiteral("&Type:"), 9, 48);
    start_type_ = make_combo(start_group, 53, 44, 164, 21,
                             {QStringLiteral("Dashed"), QStringLiteral("Framed"),
                              QStringLiteral("Solid")},
                             false);
    add_field_label(start_group, QStringLiteral("&Color:"), 9, 72);
    start_colour_ = new QComboBox(start_group);
    place(start_colour_, 53, 68, 164, 21);
    fill_bar_colour_combo(start_colour_);

    auto* middle_group = add_group(QStringLiteral("Middle"), 252, 230);
    add_field_label(middle_group, QStringLiteral("Sh&ape:"), 9, 24);
    middle_shape_ = make_combo(
        middle_group, 58, 20, 163, 21,
        {QStringLiteral("None"), QStringLiteral("Rectangle"),
         QStringLiteral("Rectangle Top"), QStringLiteral("Rectangle Middle"),
         QStringLiteral("Rectangle Bottom"), QStringLiteral("Line Top"),
         QStringLiteral("Line Middle"), QStringLiteral("Line Bottom")},
        false);
    add_field_label(middle_group, QStringLiteral("Patte&rn:"), 9, 48);
    middle_pattern_ = make_combo(
        middle_group, 58, 44, 163, 21,
        {QStringLiteral("Hollow"), QStringLiteral("Solid Fill"),
         QStringLiteral("Light Fill"), QStringLiteral("Medium Fill"),
         QStringLiteral("Dark Fill"), QStringLiteral("Diagonal Left"),
         QStringLiteral("Diagonal Right"), QStringLiteral("Diagonal Cross"),
         QStringLiteral("Line Vertical"), QStringLiteral("Line Horizontal"),
         QStringLiteral("Line Cross"), QStringLiteral("Dashed Border")},
        false);
    add_field_label(middle_group, QStringLiteral("C&olor:"), 9, 72);
    middle_colour_ = new QComboBox(middle_group);
    place(middle_colour_, 58, 68, 163, 21);
    fill_bar_colour_combo(middle_colour_);

    auto* end_group = add_group(QStringLiteral("End"), 491, 225);
    add_field_label(end_group, QStringLiteral("Sh&ape:"), 9, 24);
    end_shape_ = make_combo(end_group, 53, 20, 163, 21,
                           {QStringLiteral("No Shape"), QStringLiteral("House Up"),
                            QStringLiteral("House Down"), QStringLiteral("Diamond"),
                            QStringLiteral("Circle Diamond"),
                            QStringLiteral("Triangle Up"),
                            QStringLiteral("Triangle Down"),
                            QStringLiteral("Triangle Right"),
                            QStringLiteral("Triangle Left"),
                            QStringLiteral("Circle Triangle Up"),
                            QStringLiteral("Circle Triangle Down"),
                            QStringLiteral("Arrow Up"), QStringLiteral("Arrow Down"),
                            QStringLiteral("Circle Arrow Up"),
                            QStringLiteral("Circle Arrow Down"),
                            QStringLiteral("Caret Down Top"),
                            QStringLiteral("Caret Up Bottom"),
                            QStringLiteral("Line Shape"), QStringLiteral("Square"),
                            QStringLiteral("Circle"), QStringLiteral("Star"),
                            QStringLiteral("Left Brace"), QStringLiteral("Right Brace"),
                            QStringLiteral("Fade Left"), QStringLiteral("Fade right")},
                           false);
    add_field_label(end_group, QStringLiteral("T&ype:"), 9, 48);
    end_type_ = make_combo(end_group, 53, 44, 163, 21,
                           {QStringLiteral("Dashed"), QStringLiteral("Framed"),
                            QStringLiteral("Solid")},
                           false);
    add_field_label(end_group, QStringLiteral("Colo&r:"), 9, 72);
    end_colour_ = new QComboBox(end_group);
    place(end_colour_, 53, 68, 163, 21);
    fill_bar_colour_combo(end_colour_);
    tabs_->addTab(bars_page, QStringLiteral("Bars"));

    note_ = make_label(
        this,
        QStringLiteral("Changes apply to this view for the session; Save keeps the "
                       "file's bytes. Appearance editing is not implemented in this "
                       "port."),
        262, 490, 479, 14, false);
    QFont small = note_->font();
    small.setPointSize(7);
    note_->setFont(small);

    help_ = make_button(this, QStringLiteral("Help"), 6, 506, 75, 23, false);
    ok_ = make_button(this, QStringLiteral("OK"), 582, 506, 75, 23, true);
    ok_->setObjectName(QStringLiteral("okButton"));
    cancel_ = make_button(this, QStringLiteral("Cancel"), 666, 506, 75, 23, true);
    cancel_->setObjectName(QStringLiteral("cancelButton"));

    populate_table();
    tabs_->setCurrentIndex(0);
    text_table_->selectRow(0);

    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { load_row(row); });
    const auto colour_changed = [this](int side, QComboBox* combo) {
        if (loading_ || source_.bars.empty()) {
            return;
        }
        apply_colour(side, combo);
    };
    connect(start_colour_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, colour_changed](int) { colour_changed(0, start_colour_); });
    connect(middle_colour_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, colour_changed](int) { colour_changed(1, middle_colour_); });
    connect(end_colour_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, colour_changed](int) { colour_changed(2, end_colour_); });
    connect(ok_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);

    if (source_.bars.empty()) {
        table_->setEnabled(false);
        text_table_->setEnabled(false);
        ok_->setEnabled(false);
        ok_->setToolTip(kDisabledTip);
        note_->setText(QStringLiteral(
            "No decoded bar style in this project; there is nothing to apply."));
    }
    if (!source_.decoded) {
        for (QWidget* control :
             {static_cast<QWidget*>(table_), static_cast<QWidget*>(tabs_),
              static_cast<QWidget*>(start_colour_), static_cast<QWidget*>(middle_colour_),
              static_cast<QWidget*>(end_colour_)}) {
            control->setEnabled(false);
            control->setToolTip(kDisabledTip);
        }
        ok_->setEnabled(false);
        ok_->setToolTip(kDisabledTip);
    }
    if (!working_.bars.empty()) {
        load_row(0);
        table_->selectRow(0);
        table_->setCurrentCell(0, 0);
    }
}

void BarStylesDialog::populate_table() {
    table_->setRowCount(static_cast<int>(working_.bars.size()));
    for (int row = 0; row < static_cast<int>(working_.bars.size()); ++row) {
        const msproject::viewstate::BarStyle& bar =
            working_.bars[static_cast<std::size_t>(row)];
        const auto set = [this, row](int column, const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table_->setItem(row, column, item);
        };
        set(0, QString::fromStdString(bar.name));
        auto* appearance = new QTableWidgetItem(bar_appearance_icon(bar), QString());
        appearance->setFlags(appearance->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 1, appearance);
        set(2, show_for_text(bar.show_for, bar.show_for_not));
        set(3, QString::number(bar.row + 1));
        set(4, from_to_field_text(bar.from_field, true));
        set(5, from_to_field_text(bar.to_field, false));
        table_->setRowHeight(row, 18);
    }
}

int BarStylesDialog::selected_row() const {
    if (table_ == nullptr) {
        return -1;
    }
    const int row = table_->currentRow();
    if (row < 0 && table_->selectionModel() != nullptr) {
        const auto rows = table_->selectionModel()->selectedRows();
        if (!rows.isEmpty()) {
            return rows.first().row();
        }
    }
    return row;
}

void BarStylesDialog::select_row(int row) {
    if (row < 0 || row >= static_cast<int>(working_.bars.size())) {
        return;
    }
    table_->selectRow(row);
    table_->setCurrentCell(row, 0);
}

void BarStylesDialog::load_row(int row) {
    if (row < 0 || row >= static_cast<int>(working_.bars.size())) {
        return;
    }
    loading_ = true;
    const msproject::viewstate::BarStyle& bar =
        working_.bars[static_cast<std::size_t>(row)];
    start_shape_->setCurrentIndex(bar.start_shape < start_shape_->count()
                                      ? bar.start_shape
                                      : 0);
    start_type_->setCurrentIndex(bar.start_type < start_type_->count()
                                     ? bar.start_type
                                     : 0);
    select_bar_colour(start_colour_, bar.has_start_colour, bar.start_colour);
    middle_shape_->setCurrentIndex(bar.middle_shape < middle_shape_->count()
                                       ? bar.middle_shape
                                       : 0);
    middle_pattern_->setCurrentIndex(bar.middle_pattern < middle_pattern_->count()
                                         ? bar.middle_pattern
                                         : 0);
    select_bar_colour(middle_colour_, bar.has_middle_colour, bar.middle_colour);
    end_shape_->setCurrentIndex(bar.end_shape < end_shape_->count() ? bar.end_shape
                                                                    : 0);
    end_type_->setCurrentIndex(bar.end_type < end_type_->count() ? bar.end_type : 0);
    select_bar_colour(end_colour_, bar.has_end_colour, bar.end_colour);
    select_bar_field(left_field_, bar.left_text);
    select_bar_field(right_field_, bar.right_text);
    select_bar_field(top_field_, bar.top_text);
    select_bar_field(bottom_field_, bar.bottom_text);
    select_bar_field(inside_field_, bar.inside_text);
    refresh_text_table(row);
    loading_ = false;
}

void BarStylesDialog::refresh_text_table(int row) {
    if (row < 0 || row >= static_cast<int>(working_.bars.size())) {
        return;
    }
    const msproject::viewstate::BarStyle& bar =
        working_.bars[static_cast<std::size_t>(row)];
    QComboBox* combos[5] = {left_field_, right_field_, top_field_, bottom_field_,
                            inside_field_};
    const std::uint32_t fields[5] = {bar.left_text, bar.right_text, bar.top_text,
                                     bar.bottom_text, bar.inside_text};
    for (int side = 0; side < 5; ++side) {
        if (combos[side] != nullptr) {
            select_bar_field(combos[side], fields[side]);
        }
    }
}

void BarStylesDialog::apply_text_field(int side) {
    if (loading_) {
        return;
    }
    const int row = selected_row();
    if (row < 0 || row >= static_cast<int>(working_.bars.size())) {
        return;
    }
    msproject::viewstate::BarStyle& bar =
        working_.bars[static_cast<std::size_t>(row)];
    QComboBox* combos[5] = {left_field_, right_field_, top_field_, bottom_field_,
                            inside_field_};
    std::uint32_t* fields[5] = {&bar.left_text, &bar.right_text, &bar.top_text,
                                &bar.bottom_text, &bar.inside_text};
    if (side < 0 || side >= 5) {
        return;
    }
    *fields[side] = selected_bar_field(combos[side]);
    changed_ = true;
}

void BarStylesDialog::apply_colour(int side, const QComboBox* combo) {
    const int row = selected_row();
    if (row < 0 || row >= static_cast<int>(working_.bars.size())) {
        return;
    }
    msproject::viewstate::BarStyle& bar =
        working_.bars[static_cast<std::size_t>(row)];
    bool has = false;
    std::uint32_t key = combo_colour(combo, has);
    switch (side) {
        case 0:
            apply_rgb(key, has, bar.start_colour);
            bar.has_start_colour = has;
            break;
        case 1:
            apply_rgb(key, has, bar.middle_colour);
            bar.has_middle_colour = has;
            break;
        case 2:
            apply_rgb(key, has, bar.end_colour);
            bar.has_end_colour = has;
            break;
        default:
            return;
    }
    changed_ = true;
}

void BarStylesDialog::accept() {
    if (!source_.decoded || source_.bars.empty()) {
        return;
    }
    result_ = working_;
    QDialog::accept();
}

}  // namespace msqt
