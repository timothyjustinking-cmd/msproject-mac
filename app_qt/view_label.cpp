// app_qt/view_label.cpp -- see view_label.h.

#include "view_label.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QSize>
#include <QSizePolicy>
#include <QString>

#include "shell_style.h"

namespace msqt {
namespace {

const QColor kLabelBackground(0xf7, 0xf7, 0xf7);
const QColor kLabelText(0x4a, 0x4a, 0x4a);
const QColor kLabelBorder(0xd0, 0xd0, 0xd0);
constexpr int kLabelWidth = 22;

}  // namespace

ViewLabel::ViewLabel(const QString& text, QWidget* parent)
    : QWidget(parent), text_(text) {
    setObjectName(QStringLiteral("viewLabel"));
    setFixedWidth(kLabelWidth);
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAutoFillBackground(false);
}

QSize ViewLabel::sizeHint() const { return QSize(kLabelWidth, 240); }

QSize ViewLabel::minimumSizeHint() const { return QSize(kLabelWidth, 120); }

void ViewLabel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kLabelBackground);
    painter.setPen(QPen(kLabelBorder, 1));
    painter.drawLine(width() - 1, 0, width() - 1, height());

    QFont font;
    font.setPointSize(kStatusPointSize);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    painter.setFont(font);
    painter.setPen(kLabelText);
    // Rotate around the strip's centre, reading bottom-to-top like the product's
    // vertical view caption.  The painter's own bounding rect is measured with
    // the rotated font metrics so the text is never clipped at either end.
    const QFontMetrics metrics(font);
    const int text_width = metrics.horizontalAdvance(text_);
    const int centre_x = width() / 2;
    const int centre_y = height() / 2;
    painter.translate(centre_x, centre_y);
    painter.rotate(-90.0);
    painter.drawText(QRect(-text_width / 2, -12, text_width, 24),
                     static_cast<int>(Qt::AlignCenter), text_);
}

}  // namespace msqt
