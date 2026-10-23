// app_qt/preferences_store.cpp -- see preferences_store.h.

#include "preferences_store.h"

#include <QByteArray>
#include <QString>

#include <string>

namespace msqt {

PreferencesStore::PreferencesStore() : settings_(std::make_unique<QSettings>()) {}

PreferencesStore::PreferencesStore(const QString& file_path)
    : settings_(std::make_unique<QSettings>(file_path, QSettings::IniFormat)) {}

bool PreferencesStore::load(app::Preferences& out) const {
    if (settings_ == nullptr) {
        return false;
    }
    const QString key = QString::fromLatin1(app::kPreferencesDefaultsKey);
    if (!settings_->contains(key)) {
        return false;
    }
    const QString stored = settings_->value(key).toString();
    if (stored.isEmpty()) {
        return false;
    }
    app::Preferences parsed = out;
    if (!app::parse_preferences(stored.toStdString(), parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

void PreferencesStore::save(const app::Preferences& preferences) {
    if (settings_ == nullptr) {
        return;
    }
    const QString key = QString::fromLatin1(app::kPreferencesDefaultsKey);
    settings_->setValue(key, QString::fromStdString(app::serialize_preferences(preferences)));
    settings_->sync();
}

bool PreferencesStore::writable() const {
    if (settings_ == nullptr) {
        return false;
    }
    return settings_->status() == QSettings::NoError;
}

namespace {

QString theme_key() {
    return QString::fromLatin1(app::kPreferencesDefaultsKey) + QStringLiteral(".theme");
}

}  // namespace

bool PreferencesStore::load_theme(Theme& out) const {
    if (settings_ == nullptr || !settings_->contains(theme_key())) {
        return false;
    }
    Theme parsed = out;
    if (!theme_from_name(settings_->value(theme_key()).toString().toStdString(), parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

void PreferencesStore::save_theme(Theme theme) {
    if (settings_ == nullptr) {
        return;
    }
    settings_->setValue(theme_key(), QString::fromLatin1(theme_name(theme)));
    settings_->sync();
}

QString PreferencesStore::file_path() const {
    return settings_ != nullptr ? settings_->fileName() : QString();
}

}  // namespace msqt
