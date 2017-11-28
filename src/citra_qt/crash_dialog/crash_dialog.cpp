// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <string>
#include <QFile>
#include <fmt/format.h>
#include <glad/glad.h>
#include "citra_qt/crash_dialog/crash_dialog.h"
#include "common/crash_handler.h"
#include "common/scm_rev.h"
#include "common/ui_util.h"
#include "common/x64/cpu_detect.h"
#include "ui_crash_dialog.h"

static const char* GetGLString(GLenum name) {
    const char* str = reinterpret_cast<const char*>(glGetString(name));
    return str ? str : "(null)";
}

CrashDialog::CrashDialog(QWidget* parent, const Common::CrashInformation& crash_info)
    : QDialog(parent), ui(std::make_unique<Ui::CrashDialog>()) {
    ui->setupUi(this);

    ui->informational_box->clear();
    AddLine("Citra Crash Information");
    AddLine("===========================");
    AddLine(fmt::format("Build information: {} {}", Common::g_build_date, Common::g_build_name));
    AddLine(fmt::format("Revision: {}", Common::g_scm_rev));
    AddLine(fmt::format("Branch: {} {}", Common::g_scm_branch, Common::g_scm_desc));
    AddLine(fmt::format("CPU: {} - {}", Common::GetCPUCaps().cpu_string,
                        Common::GetCPUCaps().brand_string));
    AddLine(fmt::format("GL Version: {}", GetGLString(GL_VERSION)));
    AddLine(fmt::format("GL Vendor: {}", GetGLString(GL_VENDOR)));
    AddLine(fmt::format("GL Renderer: {}", GetGLString(GL_RENDERER)));
    AddLine("Stack trace:");
    for (const auto& line : crash_info.stack_trace) {
        AddLine(line);
    }

    ui->informational_box->moveCursor(QTextCursor::Start);
    ui->informational_box->ensureCursorVisible();

    if (crash_info.minidump_filename) {
        minidump_filename = QString::fromStdString(*crash_info.minidump_filename);
        ui->view_minidump_button->setEnabled(QFile::exists(minidump_filename));
    }
}

CrashDialog::~CrashDialog() = default;

void CrashDialog::on_view_minidump_button_released() {
    Common::ShowInFileBrowser(minidump_filename.toStdString());
}

void CrashDialog::AddLine(const std::string& str) {
    ui->informational_box->appendPlainText(QString::fromStdString(str));
}
