// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include <QDialog>

namespace Ui {
class CrashDialog;
}

namespace Common {
struct CrashInformation;
}

class CrashDialog : public QDialog {
    Q_OBJECT

public:
    CrashDialog(QWidget* parent, const Common::CrashInformation& crash_info);
    ~CrashDialog();

private slots:
    void on_view_minidump_button_released();

private:
    void AddLine(const std::string& str);

    std::unique_ptr<Ui::CrashDialog> ui;
    QString minidump_filename;
};
