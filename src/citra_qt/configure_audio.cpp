// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "audio_core/sink_details.h"

#include "citra_qt/configure_audio.h"
#include "ui_configure_audio.h"

#include "core/settings.h"

ConfigureAudio::ConfigureAudio(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ConfigureAudio) {
    ui->setupUi(this);

    ui->output_sink_combo_box->clear();
    for (const auto& sink_detail : AudioCore::g_sink_details) {
        ui->output_sink_combo_box->insertItem(0, sink_detail.name, QVariant(sink_detail.id));
    }

    this->setConfiguration();
}

ConfigureAudio::~ConfigureAudio() {
}

void ConfigureAudio::setConfiguration() {
    for (int index = 0; index < ui->output_sink_combo_box->count(); index++) {
        if (ui->output_sink_combo_box->itemData(index).toInt() == Settings::values.sink_id) {
            ui->output_sink_combo_box->setCurrentIndex(index);
            break;
        }
    }
}

void ConfigureAudio::applyConfiguration() {
    Settings::values.sink_id = ui->output_sink_combo_box->itemData(ui->output_sink_combo_box->currentIndex()).toInt();
    Settings::Apply();
}
