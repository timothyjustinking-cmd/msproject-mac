// app_qt/tests/icon_gate_test.cpp -- W26-2: the ribbon icon gate.
//
// One item per tab (W26-2.1..2.7).  For each tab the wave has finished, the
// gate proves from the LIVE widgets and their RENDERED pixels:
//   * every command control (button/small/split) carries a pin icon key, and
//     the live key is the independent pin's icon column;
//   * its rendered glyph is not the neutral placeholder at either Project
//     size (32 px large, 16 px small);
//   * the glyph is monochrome: every visible pixel is the same RGB;
//   * no two commands on the tab render the same glyph unless the pin reuses
//     one key where Project reuses one glyph (currently only View's two
//     "Other Views" entries), and no two DIFFERENT keys collide either;
//   * the live widget's own icon at its own icon size is the key's glyph;
//   * a human-review contact sheet of the tab's glyphs at both sizes is
//     written to the output directory (build/w26/ locally, for Tim).
//
// Tabs join kPlan in their item's commit; a tab is never checked before its
// glyphs exist, and the final item proves the pin's icon column has no '-' left
// on any command row.
#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "gate_util.h"
#include "main_window.h"
#include "menu_model.h"
#include "ribbon_bar.h"
#include "ribbon_glyphs.h"
#include "shell_icons.h"
#include "shell_style.h"

namespace {

constexpr int kLargePx = 32;
constexpr int kSmallPx = 16;

struct PinControl {
    std::string group;
    std::string label;
    std::string kind;
    std::string icon;
};

// A command glyph the pin expects to appear more than once on its tab because
// Project reuses one glyph (View's Task Views / Resource Views "Other Views").
struct Reuse {
    const char* key;
    int count;
};

struct TabPlan {
    const char* title;
    std::vector<Reuse> reuse;
};

// The tabs whose item is DONE.  Each W26-2.x commit appends its tab; the last
// one also flips kAllTabsChecked.
const std::vector<TabPlan> kPlan = {
    {"Task", {}},
    {"Resource", {}},
    {"Report", {}},
    {"Project", {}},
    // View's two "Other Views" entries are Project's one same-tab glyph reuse.
    {"View", {{"view.other", 2}}},
    {"Help", {}},
    {"Gantt Chart Format", {}},
};
constexpr bool kAllTabsChecked = true;

struct LiveCommand {
    QString group;
    QString label;
    QString key;
    QWidget* widget = nullptr;
};

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    for (const char c : line) {
        if (c == '\t') {
            fields.push_back(field);
            field.clear();
        } else {
            field.push_back(c);
        }
    }
    fields.push_back(field);
    return fields;
}

std::map<std::string, std::vector<PinControl>> load_pin(const QString& path) {
    std::map<std::string, std::vector<PinControl>> tabs;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return tabs;
    }
    std::string tab;
    std::string group;
    while (!file.atEnd()) {
        const std::string raw = file.readLine().toStdString();
        if (raw.empty() || raw[0] == '#') {
            continue;
        }
        std::string line = raw;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        const std::vector<std::string> fields = split_tabs(line);
        if (fields.size() < 9) {
            continue;
        }
        tab = fields[0];
        if (fields[3] == "group") {
            group = fields[2];
            continue;
        }
        if (fields[3] != "button" && fields[3] != "small" && fields[3] != "split") {
            continue;
        }
        PinControl control;
        control.group = group;
        control.label = fields[4];
        control.kind = fields[3];
        control.icon = fields[6];
        tabs[tab].push_back(control);
    }
    return tabs;
}

QString normalized(const QString& text) {
    QString out = text;
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return out.simplified();
}

QString live_label(const QWidget* control) {
    if (const auto* button = qobject_cast<const QToolButton*>(control)) {
        return button->text().isEmpty() ? button->accessibleName() : button->text();
    }
    return control != nullptr ? control->accessibleName() : QString();
}

QImage rendered_icon(const QIcon& icon, int pixels, QImage::Format format) {
    return icon.pixmap(QSize(pixels, pixels)).toImage().convertToFormat(format);
}

QImage glyph_image(const QString& key, int pixels) {
    return rendered_icon(msqt::shell_ribbon_icon(key, pixels), pixels, QImage::Format_ARGB32);
}

// The glyph as QPainter actually blended it: premultiplied, so an antialiased
// edge is the ink scaled by its alpha and the monochrome test can read that
// relation directly.
QImage glyph_blend(const QString& key, int pixels) {
    return rendered_icon(msqt::shell_ribbon_icon(key, pixels), pixels,
                         QImage::Format_ARGB32_Premultiplied);
}

QByteArray glyph_hash(const QImage& image) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(image.width()));
    hash.addData(QByteArray::number(image.height()));
    hash.addData(QByteArray(reinterpret_cast<const char*>(image.constBits()),
                            static_cast<int>(image.sizeInBytes())));
    return hash.result();
}

QByteArray placeholder_hash(int pixels) {
    return glyph_hash(msqt::shell_action_icon(app::MenuAction::none, pixels)
                          .pixmap(QSize(pixels, pixels))
                          .toImage()
                          .convertToFormat(QImage::Format_ARGB32));
}

int opaque_pixels(const QImage& image, int minimum_alpha = 128) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) >= minimum_alpha) {
                ++count;
            }
        }
    }
    return count;
}

// Every visible pixel must be a blend of ONE ink with transparency.  `image`
// is the premultiplied render, so at alpha `a` the expected channels are
// ink * a / 255.  Returns the number of pixels that deviate by more than 5/255
// (premultiplied rounding), or -1 when the glyph has no visible pixel at all.
int off_ink_pixels(const QImage& image) {
    int ink_r = -1;
    int ink_g = -1;
    int ink_b = -1;
    int max_alpha = 0;
    // The most opaque pixel, scaled to full ink, names the colour.
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            const int alpha = qAlpha(pixel);
            if (alpha > max_alpha) {
                max_alpha = alpha;
                const double scale = 255.0 / alpha;
                ink_r = std::min(255, static_cast<int>(qRed(pixel) * scale + 0.5));
                ink_g = std::min(255, static_cast<int>(qGreen(pixel) * scale + 0.5));
                ink_b = std::min(255, static_cast<int>(qBlue(pixel) * scale + 0.5));
            }
        }
    }
    if (max_alpha == 0) {
        return -1;
    }
    int off = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            const int alpha = qAlpha(pixel);
            if (alpha < 8) {
                continue;
            }
            const int expected_r = ink_r * alpha / 255;
            const int expected_g = ink_g * alpha / 255;
            const int expected_b = ink_b * alpha / 255;
            if (std::abs(qRed(pixel) - expected_r) > 5 ||
                std::abs(qGreen(pixel) - expected_g) > 5 ||
                std::abs(qBlue(pixel) - expected_b) > 5) {
                ++off;
            }
        }
    }
    return off;
}

QString slug(const QString& title) {
    QString out = title.toLower();
    for (QChar& character : out) {
        if (!character.isLetterOrNumber()) {
            character = QLatin1Char('-');
        }
    }
    return out;
}

// The human-review sheet: every command on the tab, its 32 px and 16 px glyph,
// its pin label and its icon key.
bool write_contact_sheet(const QString& path, const QString& tab,
                         const std::vector<LiveCommand>& commands) {
    const int columns = 3;
    const int cell_w = 380;
    const int cell_h = 86;
    const int margin = 16;
    const int title_h = 40;
    const int rows = (static_cast<int>(commands.size()) + columns - 1) / columns;
    QImage sheet(margin * 2 + columns * cell_w,
                 title_h + margin + rows * cell_h + margin, QImage::Format_ARGB32);
    sheet.fill(QColor(0xff, 0xff, 0xff));
    QPainter painter(&sheet);
    painter.setRenderHint(QPainter::Antialiasing, false);
    QFont title_font;
    title_font.setPointSize(14);
    title_font.setBold(true);
    painter.setFont(title_font);
    painter.setPen(QColor(0x1f, 0x1f, 0x1f));
    painter.drawText(QRect(margin, 6, sheet.width() - 2 * margin, 30),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("W26-2 icons - %1 tab (32 px and 16 px, monochrome)")
                         .arg(tab));
    QFont label_font;
    label_font.setPointSize(9);
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const LiveCommand& command = commands[index];
        const int column = static_cast<int>(index) % columns;
        const int row = static_cast<int>(index) / columns;
        const int x = margin + column * cell_w;
        const int y = title_h + margin + row * cell_h;
        painter.setPen(QColor(0xdd, 0xdd, 0xdd));
        painter.drawRect(QRect(x, y, cell_w - 8, cell_h - 8));
        painter.drawImage(QPoint(x + 10, y + 12), glyph_image(command.key, kLargePx));
        painter.drawImage(QPoint(x + 52, y + 20), glyph_image(command.key, kSmallPx));
        painter.setFont(label_font);
        painter.setPen(QColor(0x1f, 0x1f, 0x1f));
        painter.drawText(QRect(x + 86, y + 10, cell_w - 100, 22),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1 / %2").arg(command.group, command.label));
        painter.setPen(QColor(0x66, 0x66, 0x66));
        painter.drawText(QRect(x + 86, y + 34, cell_w - 100, 20),
                         Qt::AlignLeft | Qt::AlignVCenter, command.key);
        painter.drawText(QRect(x + 86, y + 54, cell_w - 100, 20),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("32 px / 16 px"));
    }
    painter.end();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const bool saved = sheet.save(path, "PNG");
    gate::check(saved, "the tab's contact sheet is written");
    return saved;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    msqt::configure_application();
    if (argc < 3) {
        std::fprintf(stderr, "usage: icon_gate_tests <ribbon_pin.tsv> <contact-sheet-dir>\n");
        return 2;
    }
    const QString pin_path = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);

    const std::map<std::string, std::vector<PinControl>> pin = load_pin(pin_path);
    gate::check(!pin.empty(), "the independent ribbon pin loads");

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, msqt::kParityHeight);
    window.show();
    QApplication::processEvents();
    msqt::RibbonBar* ribbon = window.ribbon();
    gate::check(ribbon != nullptr, "the window owns a ribbon");
    if (ribbon == nullptr) {
        std::printf("qt_icon_gate: ");
        return gate::finish();
    }

    const QByteArray placeholder_large = placeholder_hash(kLargePx);
    const QByteArray placeholder_small = placeholder_hash(kSmallPx);

    for (const TabPlan& plan : kPlan) {
        const QString tab = QString::fromUtf8(plan.title);
        const std::string where = std::string("tab ") + plan.title;
        const int tab_index = ribbon->tab_titles().indexOf(tab);
        gate::check(tab_index >= 0, (where + ": the tab exists").c_str());
        if (tab_index < 0) {
            continue;
        }
        ribbon->set_active_tab(tab_index);
        QApplication::processEvents();

        // The live command controls, in ribbon order, with the pin's rows.
        std::vector<LiveCommand> commands;
        for (int group = 0; group < ribbon->group_titles().size(); ++group) {
            const QString group_title = ribbon->group_titles()[group];
            for (int control = 0; control < ribbon->group_control_count(group); ++control) {
                QWidget* live = ribbon->group_control(group, control);
                const QString kind = msqt::RibbonBar::control_kind(live);
                if (kind != QStringLiteral("button") && kind != QStringLiteral("small") &&
                    kind != QStringLiteral("split")) {
                    continue;
                }
                LiveCommand command;
                command.group = group_title;
                command.label = normalized(live_label(live));
                command.key = ribbon->group_control_icon_key(group, control);
                command.widget = live;
                commands.push_back(command);
            }
        }

        const std::vector<PinControl> pin_commands =
            pin.count(plan.title) ? pin.at(plan.title) : std::vector<PinControl>();
        gate::check(commands.size() == pin_commands.size(),
                    (where + ": the live command count is the pin's").c_str());

        std::map<QString, int> key_counts;
        std::map<QByteArray, std::vector<QString>> hash_groups_large;
        std::map<QByteArray, std::vector<QString>> hash_groups_small;
        const std::size_t common = std::min(commands.size(), pin_commands.size());
        for (std::size_t index = 0; index < common; ++index) {
            const LiveCommand& command = commands[index];
            const PinControl& expected = pin_commands[index];
            const std::string label = command.label.toStdString();
            gate::check(command.group.toStdString() == expected.group &&
                            label == normalized(QString::fromUtf8(expected.label.c_str()))
                                .toStdString(),
                        (where + ": command order and labels are the pin's").c_str());
            gate::check(!command.key.isEmpty(),
                        (where + " / " + label + ": a command has an icon key").c_str());
            gate::check(expected.icon != "-",
                        (where + " / " + label + ": the pin's icon column is filled").c_str());
            gate::check(command.key.toStdString() == expected.icon,
                        (where + " / " + label +
                         ": the live icon key is the independent pin's")
                            .c_str());
            gate::check(msqt::shell_ribbon_glyph_known(command.key),
                        (where + " / " + label + ": the icon key has a drawing").c_str());
            ++key_counts[command.key];

            // The glyph at both Project sizes: non-empty, monochrome and never
            // the neutral placeholder.
            for (const int pixels : {kLargePx, kSmallPx}) {
                const QImage image = glyph_image(command.key, pixels);
                gate::check(image.width() == pixels && image.height() == pixels,
                            (where + " / " + label + ": the glyph is drawn at its size")
                                .c_str());
                gate::check(opaque_pixels(image) >= 5,
                            (where + " / " + label + ": the glyph draws visible ink at " +
                             std::to_string(pixels) + " px")
                                .c_str());
                gate::check(opaque_pixels(image, 180) > 0,
                            (where + " / " + label + ": the glyph has solid ink at " +
                             std::to_string(pixels) + " px")
                                .c_str());
                const int off = off_ink_pixels(glyph_blend(command.key, pixels));
                gate::check(off == 0,
                            (where + " / " + label + ": the glyph is monochrome at " +
                             std::to_string(pixels) + " px")
                                .c_str());
                if (off != 0) {
                    std::fprintf(stderr, "  %s / %s: %d off-ink pixels at %d px\n",
                                 plan.title, label.c_str(), off, pixels);
                }
                const QByteArray hash = glyph_hash(image);
                gate::check(hash != (pixels == kLargePx ? placeholder_large : placeholder_small),
                            (where + " / " + label + ": the glyph is not the placeholder at " +
                             std::to_string(pixels) + " px")
                                .c_str());
                (pixels == kLargePx ? hash_groups_large : hash_groups_small)[hash].push_back(
                    command.key);
            }

            // The live widget's own icon at its own icon size is this glyph.
            auto* button = qobject_cast<QAbstractButton*>(command.widget);
            gate::check(button != nullptr, (where + " / " + label + ": is a button").c_str());
            if (button != nullptr) {
                const int live_px = button->iconSize().width();
                gate::check((live_px == kLargePx || live_px == kSmallPx) &&
                                button->iconSize().height() == live_px,
                            (where + " / " + label +
                             ": the live icon is one of Project's two sizes")
                                .c_str());
                if (live_px == kLargePx || live_px == kSmallPx) {
                    const QImage live_image =
                        button->icon()
                            .pixmap(QSize(live_px, live_px))
                            .toImage()
                            .convertToFormat(QImage::Format_ARGB32);
                    gate::check(glyph_hash(live_image) ==
                                    glyph_hash(glyph_image(command.key, live_px)),
                                (where + " / " + label +
                                 ": the live widget shows the key's glyph")
                                    .c_str());
                }
                if (msqt::RibbonBar::control_kind(command.widget) == QStringLiteral("button")) {
                    gate::check(button->iconSize().width() == kLargePx,
                                (where + " / " + label +
                                 ": a large button carries the 32 px glyph")
                                    .c_str());
                } else if (msqt::RibbonBar::control_kind(command.widget) ==
                           QStringLiteral("small")) {
                    gate::check(button->iconSize().width() == kSmallPx,
                                (where + " / " + label +
                                 ": a one-row button carries the 16 px glyph")
                                    .c_str());
                }
            }
        }

        // Duplicates: a key may repeat only where the pin/Project reuses the
        // glyph; two different keys must never render identical pixels.
        for (const auto& entry : key_counts) {
            int allowed = 1;
            for (const Reuse& reuse : plan.reuse) {
                if (entry.first == QString::fromUtf8(reuse.key)) {
                    allowed = reuse.count;
                }
            }
            gate::check(entry.second == allowed,
                        (where + ": glyph key '" + entry.first.toStdString() +
                         "' is used its expected number of times")
                            .c_str());
        }
        for (const auto* groups : {&hash_groups_large, &hash_groups_small}) {
            for (const auto& entry : *groups) {
                if (entry.second.size() <= 1) {
                    continue;
                }
                // A hash repeat is legitimate only when every command in the
                // group shares one key that is listed as reused with that count.
                bool shared_key = true;
                for (const QString& key : entry.second) {
                    if (key != entry.second.front()) {
                        shared_key = false;
                    }
                }
                bool listed = false;
                for (const Reuse& reuse : plan.reuse) {
                    if (entry.second.front() == QString::fromUtf8(reuse.key) &&
                        static_cast<int>(entry.second.size()) == reuse.count) {
                        listed = true;
                    }
                }
                gate::check(shared_key && listed,
                            (where + ": no unexpected duplicate glyph bytes").c_str());
                if (!(shared_key && listed)) {
                    std::fprintf(stderr, "  duplicate glyph: %s x%d\n",
                                 entry.second.front().toStdString().c_str(),
                                 static_cast<int>(entry.second.size()));
                }
            }
        }

        // The human-review sheet (both sizes of every command).
        const QString sheet_path = QDir(out_dir).filePath(
            QStringLiteral("icons-%1.png").arg(slug(tab)));
        if (write_contact_sheet(sheet_path, tab, commands)) {
            std::fprintf(stderr, "note: contact sheet %s (%zu commands)\n",
                         sheet_path.toLocal8Bit().constData(), commands.size());
        }
        gate::check(QFile::exists(sheet_path), "the contact sheet exists on disk");
    }

    // The final item proves no command row anywhere still carries '-' in the
    // pin's icon column.
    if (kAllTabsChecked) {
        for (const auto& entry : pin) {
            for (const PinControl& control : entry.second) {
                gate::check(control.icon != "-",
                            ("pin command " + entry.first + " / " + control.group + " / " +
                             control.label + " has an icon key")
                                .c_str());
            }
        }
    }

    std::printf("qt_icon_gate: ");
    return gate::finish();
}
