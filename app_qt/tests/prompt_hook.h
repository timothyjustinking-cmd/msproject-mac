// app_qt/tests/prompt_hook.h -- wave 35 (W35-0.3, CI-4).
//
// Answers the one modal prompt a shell click opens (QInputDialog, the removal
// QMessageBox, or any other QDialog, which is rejected) so a gate test cannot
// hang, and records what it saw.
//
// Why a poll and not QTimer::singleShot(0): a zero-delay timer looks once, at
// the first turn of the event loop after the click, and whether the dialog is
// already visible at that turn is the platform's business.  The macOS CI run of
// wave 34b failed "an assigned task is offered the removal" and nothing else
// (no macOS machine here; the shell has no platform branch), so the one thing
// in that check that depends on timing is the single look.  The poll looks every
// few milliseconds until it answers or gives up, and the answerer's destructor
// stops it, so a click that opens no prompt cannot leave a timer that answers an
// unrelated dialog later.
//
// `found` lists the visible top-level widgets when the poll gave up, so a test
// can print it and a CI failure names what the test saw.
#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace gate {

struct PromptResult {
    bool seen = false;
    bool question = false;  // the prompt was a QMessageBox (the removal question)
    QString title;
    QString initial;        // a QInputDialog's text when it opened
    QString found;          // what was visible when the poll gave up
};

class PromptAnswerer {
public:
    // `accept` answers Yes / the chosen item; otherwise No / Cancel.
    PromptAnswerer(PromptResult* result, bool accept, const QString& answer)
        : result_(result), accept_(accept), answer_(answer) {
        timer_.setInterval(5);
        QObject::connect(&timer_, &QTimer::timeout, [this]() { poll(); });
        timer_.start();
    }

    ~PromptAnswerer() { timer_.stop(); }

    PromptAnswerer(const PromptAnswerer&) = delete;
    PromptAnswerer& operator=(const PromptAnswerer&) = delete;

private:
    static constexpr int kMaxPolls = 400;  // 2 s: far past any real dialog show

    bool answer(QWidget* widget) {
        if (auto* input = qobject_cast<QInputDialog*>(widget)) {
            result_->seen = true;
            result_->title = input->windowTitle();
            result_->initial = input->textValue();
            if (accept_) {
                input->setTextValue(answer_);
                input->accept();
            } else {
                input->reject();
            }
            return true;
        }
        if (auto* box = qobject_cast<QMessageBox*>(widget)) {
            result_->seen = true;
            result_->question = true;
            result_->title = box->windowTitle();
            QAbstractButton* button = box->button(accept_ ? QMessageBox::Yes : QMessageBox::No);
            if (button != nullptr) {
                button->click();
            } else {
                box->reject();
            }
            return true;
        }
        if (auto* dialog = qobject_cast<QDialog*>(widget)) {
            result_->seen = true;
            result_->title = dialog->windowTitle();
            dialog->reject();
            return true;
        }
        return false;
    }

    void poll() {
        if (QWidget* modal = QApplication::activeModalWidget()) {
            if (modal->isVisible() && answer(modal)) {
                timer_.stop();
                return;
            }
        }
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->isVisible() && answer(widget)) {
                timer_.stop();
                return;
            }
        }
        if (++polls_ >= kMaxPolls) {
            timer_.stop();
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (widget->isVisible()) {
                    result_->found += QString::fromLatin1(widget->metaObject()->className()) +
                                      QLatin1Char(' ');
                }
            }
        }
    }

    PromptResult* result_;
    bool accept_;
    QString answer_;
    QTimer timer_;
    int polls_ = 0;
};

}  // namespace gate
