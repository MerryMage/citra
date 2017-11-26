// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include "common/crash_handler.h"
#include "common/scm_rev.h"
#include "crash_dialog.h"
#include "ui_crash_dialog.h"

CrashDialog::CrashDialog(QWidget* parent, const Common::CrashInformation& crash_info)
    : QDialog(parent), ui(std::make_unique<Ui::CrashDialog>()) {
    ui->setupUi(this);

    ui->informational_box->clear();
    ui->informational_box->appendPlainText("Citra Crash Information");
    ui->informational_box->appendPlainText("===========================");
    ui->informational_box->appendPlainText("Build information:");
    ui->informational_box->appendPlainText(Common::g_build_date);
    ui->informational_box->appendPlainText(Common::g_build_name);
    ui->informational_box->appendPlainText("Revision:");
    ui->informational_box->appendPlainText(Common::g_scm_rev);
    ui->informational_box->appendPlainText("Branch:");
    ui->informational_box->appendPlainText(Common::g_scm_branch);
    ui->informational_box->appendPlainText(Common::g_scm_desc);
    ui->informational_box->appendPlainText("Stack trace:");
    for (const auto& line : crash_info.stack_trace) {
        ui->informational_box->appendPlainText(QString::fromStdString(line));
    }

    ui->informational_box->moveCursor(QTextCursor::Start);
    ui->informational_box->ensureCursorVisible();
}

CrashDialog::~CrashDialog() = default;

void CrashDialog::on_view_minidump_button_released() {
    // QMessageBox::critical(0, "Citra", "Unimplemented", QMessageBox::Ok);
}
