// app_qt/spelling_dialog.cpp -- see spelling_dialog.h.

#include "spelling_dialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "shell_style.h"

namespace msqt {
namespace {

QTableWidgetItem* make_item(const QString& text, bool right_aligned) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (right_aligned) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    return item;
}

QString names_phrase(std::size_t count) {
    const QString number = QString::number(static_cast<qulonglong>(count));
    return number + (count == 1 ? QStringLiteral(" task name")
                                : QStringLiteral(" task names"));
}

}  // namespace

SpellingDialog::SpellingDialog(const std::vector<app::SpellingIssue>& issues,
                               std::size_t names_checked, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("spellingDialog"));
    setWindowTitle(QStringLiteral("Spelling"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 10);
    outer->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("Task name spelling check"), this);
    title->setObjectName(QStringLiteral("spellingTitle"));
    QFont title_font = QApplication::font();
    title_font.setBold(true);
    title->setFont(title_font);
    outer->addWidget(title);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("spellingSummary"));
    summary_->setWordWrap(true);
    if (names_checked == 0) {
        summary_->setText(QStringLiteral("No task names to check."));
    } else if (issues.empty()) {
        summary_->setText(names_phrase(names_checked) +
                          QStringLiteral(" checked; no words outside the port's "
                                         "word list were found."));
    } else {
        summary_->setText(
            QString::number(static_cast<qulonglong>(issues.size())) +
            QStringLiteral(" of ") + names_phrase(names_checked) +
            (issues.size() == 1
                 ? QStringLiteral(" has words outside the port's word list.")
                 : QStringLiteral(" have words outside the port's word list.")));
    }
    outer->addWidget(summary_);

    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("spellingTable"));
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({QStringLiteral("ID"),
                                       QStringLiteral("Task name"),
                                       QStringLiteral("Not in the port's word list")});
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table_->setColumnWidth(0, 52);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setRowCount(static_cast<int>(issues.size()));
    for (std::size_t row = 0; row < issues.size(); ++row) {
        const app::SpellingIssue& issue = issues[row];
        table_->setItem(static_cast<int>(row), 0,
                        make_item(QString::number(issue.id), true));
        table_->setItem(static_cast<int>(row), 1,
                        make_item(QString::fromStdString(issue.name), false));
        QStringList words;
        for (const std::string& word : issue.unknown_words) {
            words.append(QString::fromStdString(word));
        }
        table_->setItem(static_cast<int>(row), 2,
                        make_item(words.join(QStringLiteral(", ")), false));
    }
    outer->addWidget(table_, 1);

    note_ = new QLabel(
        QStringLiteral("Read-only: this port checks against its own word list (%1 "
                       "words plus regular suffixes), not Microsoft Project's "
                       "dictionary, and changes nothing. A listed word may simply be "
                       "outside the list.")
            .arg(QString::number(static_cast<qulonglong>(app::spelling_word_count()))),
        this);
    note_->setObjectName(QStringLiteral("spellingNote"));
    note_->setWordWrap(true);
    note_->setFont(QFont(QString(), kStatusPointSize));
    note_->setForegroundRole(QPalette::WindowText);
    outer->addWidget(note_);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addStretch(1);
    close_ = new QPushButton(QStringLiteral("Close"), this);
    close_->setObjectName(QStringLiteral("spellingClose"));
    close_->setDefault(true);
    close_->setAutoDefault(true);
    close_->setFixedWidth(75);
    connect(close_, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close_);
    outer->addLayout(buttons);

    setMinimumSize(560, 340);
    resize(660, 380);
}

QString SpellingDialog::summary_text() const {
    return summary_ != nullptr ? summary_->text() : QString();
}

int SpellingDialog::issue_count() const noexcept {
    return table_ != nullptr ? table_->rowCount() : 0;
}

}  // namespace msqt
