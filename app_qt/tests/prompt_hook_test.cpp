// app_qt/tests/prompt_hook_test.cpp -- wave 35 (W35-0.3, CI-4).
//
// The gate tests answer the prompt a shell click opens with gate::PromptAnswerer
// (prompt_hook.h).  This test pins the hook itself on the one thing the old
// single zero-delay look could not do: answer a dialog that becomes visible some
// milliseconds after the event loop starts.  Each case would block in exec() for
// ever under singleShot(0) (ctest's timeout then fails it).
//
// Usage: qt_prompt_hook (no arguments)

#include <QApplication>
#include <QDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "gate_util.h"
#include "prompt_hook.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QWidget parent;
    parent.show();
    QApplication::processEvents();

    // A Yes/No question opened 40 ms after the loop starts: Yes is clicked.
    for (const bool accept : {true, false}) {
        gate::PromptResult result;
        QMessageBox::StandardButton choice = QMessageBox::Cancel;
        {
            gate::PromptAnswerer answerer(&result, accept, QString());
            QTimer::singleShot(40, [&]() {
                choice = QMessageBox::question(&parent, QStringLiteral("Late question"),
                                               QStringLiteral("Remove it?"),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);
            });
            QTimer loop_guard;
            loop_guard.setSingleShot(true);
            loop_guard.start(400);
            while (loop_guard.isActive()) {
                QApplication::processEvents(QEventLoop::AllEvents, 20);
            }
        }
        gate::check(result.seen && result.question, "a late QMessageBox is seen as a question");
        // A macOS QMessageBox has no window title, so the box is identified by its text.
        gate::check(result.text == QStringLiteral("Remove it?"),
                    "the late question's text is recorded");
        gate::check(choice == (accept ? QMessageBox::Yes : QMessageBox::No),
                    accept ? "Yes answers the late question" : "No answers the late question");
    }

    // An item list opened late: the chosen text is applied, a refusal cancels.
    for (const bool accept : {true, false}) {
        gate::PromptResult result;
        bool ok = !accept;
        QString chosen;
        {
            gate::PromptAnswerer answerer(&result, accept, QStringLiteral("second"));
            QTimer::singleShot(40, [&]() {
                chosen = QInputDialog::getText(&parent, QStringLiteral("Late input"),
                                               QStringLiteral("Name:"), QLineEdit::Normal,
                                               QStringLiteral("first"), &ok);
            });
            QTimer loop_guard;
            loop_guard.setSingleShot(true);
            loop_guard.start(400);
            while (loop_guard.isActive()) {
                QApplication::processEvents(QEventLoop::AllEvents, 20);
            }
        }
        gate::check(result.seen && !result.question, "a late QInputDialog is seen as an input");
        gate::check(result.initial == QStringLiteral("first"),
                    "the input's starting text is recorded");
        gate::check(ok == accept, "accepting and cancelling the late input are told apart");
        gate::check(!accept || chosen == QStringLiteral("second"),
                    "the answer is the text the test supplied");
    }

    // The answerer's destructor stops the poll: a click that opens nothing leaves
    // no timer to answer a dialog opened afterwards.
    {
        gate::PromptResult unused;
        {
            gate::PromptAnswerer answerer(&unused, true, QString());
        }
        bool closed_by_owner = false;
        QTimer::singleShot(40, [&]() {
            QMessageBox box(QMessageBox::Question, QStringLiteral("Unanswered"),
                            QStringLiteral("Not by the destroyed answerer"),
                            QMessageBox::Yes | QMessageBox::No, &parent);
            QTimer::singleShot(120, &box, [&box]() { box.reject(); });
            closed_by_owner = box.exec() == QDialog::Rejected;
        });
        QTimer loop_guard;
        loop_guard.setSingleShot(true);
        loop_guard.start(500);
        while (loop_guard.isActive()) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        gate::check(!unused.seen, "a destroyed answerer answers nothing later");
        gate::check(closed_by_owner, "the unanswered dialog was left to its owner");
    }
    return gate::finish();
}
