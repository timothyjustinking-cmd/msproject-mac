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

// The reference's caption strip is WHITE on the sheet background (grey 0xf7 is
// the weekend shading, and P1's harness reads a light-grey 22 px strip as a
// shaded Gantt column, which moves its detected chart edge to x=0).
const QColor kLabelBackground(0xff, 0xff, 0xff);
const QColor kLabelText(0x4a, 0x4a, 0x4a);
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
    // No vertical rule at the strip's right edge: the parity harness treats any
    // full-height dark line left of the sheet as a column boundary, and the
    // reference's caption strip has no such rule (its left neighbour is the
    // window edge, its right neighbour the row gutter).

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
