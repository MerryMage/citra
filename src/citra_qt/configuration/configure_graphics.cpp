// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "citra_qt/configuration/configure_graphics.h"
#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_graphics.h"

ConfigureGraphics::ConfigureGraphics(QWidget* parent)
    : QWidget(parent), ui(new Ui::ConfigureGraphics) {

    ui->setupUi(this);
    this->setConfiguration();

    ui->toggle_vsync->setEnabled(!Core::System::GetInstance().IsPoweredOn());
    ui->frame_limit->setEnabled(Settings::values.use_frame_limit);
    connect(ui->toggle_frame_limit, &QCheckBox::stateChanged, ui->frame_limit,
            &QSpinBox::setEnabled);

    ui->layoutBox->setEnabled(!Settings::values.custom_layout);

    connect(ui->renderer_comboBox, SIGNAL(currentIndexChanged(int)), this,
            SLOT(UpdateRenderer(int)));
}

ConfigureGraphics::~ConfigureGraphics() {}

void ConfigureGraphics::setConfiguration() {
    ui->renderer_comboBox->setCurrentIndex(static_cast<int>(Settings::values.renderer));
    ui->hw_shaders->setCurrentIndex(static_cast<int>(Settings::values.hw_shaders));
    ui->accurate_hardware_shader->setChecked(Settings::values.shaders_accurate_mul);
    ui->software_shader_jit->setChecked(Settings::values.use_shader_jit);
    ui->opengl_shader_jit->setChecked(Settings::values.use_shader_jit);
    ui->resolution_factor_combobox->setCurrentIndex(Settings::values.resolution_factor);
    ui->toggle_vsync->setChecked(Settings::values.use_vsync);
    ui->toggle_frame_limit->setChecked(Settings::values.use_frame_limit);
    ui->frame_limit->setValue(Settings::values.frame_limit);
    ui->layout_combobox->setCurrentIndex(static_cast<int>(Settings::values.layout_option));
    ui->swap_screen->setChecked(Settings::values.swap_screen);

    UpdateRenderer(static_cast<int>(Settings::values.renderer));
}

void ConfigureGraphics::applyConfiguration() {
    Settings::values.renderer =
        static_cast<Settings::RenderBackend>(ui->renderer_comboBox->currentIndex());
    Settings::values.hw_shaders = static_cast<Settings::HwShaders>(ui->hw_shaders->currentIndex());
    Settings::values.shaders_accurate_mul = ui->accurate_hardware_shader->isChecked();
    Settings::values.resolution_factor =
        static_cast<u16>(ui->resolution_factor_combobox->currentIndex());
    Settings::values.use_vsync = ui->toggle_vsync->isChecked();
    Settings::values.use_frame_limit = ui->toggle_frame_limit->isChecked();
    Settings::values.frame_limit = ui->frame_limit->value();
    Settings::values.layout_option =
        static_cast<Settings::LayoutOption>(ui->layout_combobox->currentIndex());
    Settings::values.swap_screen = ui->swap_screen->isChecked();

    switch (Settings::values.renderer) {
    case Settings::RenderBackend::Software:
        Settings::values.use_shader_jit = ui->software_shader_jit->isChecked();
        break;
    case Settings::RenderBackend::OpenGL:
        Settings::values.use_shader_jit = ui->opengl_shader_jit->isChecked();
        break;
    default:
        break;
    }
    Settings::Apply();
}

void ConfigureGraphics::UpdateRenderer(int val) {
    switch (static_cast<Settings::RenderBackend>(val)) {
    case Settings::RenderBackend::Software:
        ui->opengl_render_group->hide();
        ui->software_render_group->show();
        break;
    case Settings::RenderBackend::OpenGL:
        ui->opengl_render_group->show();
        ui->software_render_group->hide();
        break;
    default:
        break;
    }
}

void ConfigureGraphics::retranslateUi() {
    ui->retranslateUi(this);
}
