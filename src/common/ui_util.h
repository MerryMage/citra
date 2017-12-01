// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>

namespace Common {

/**
 * Opens the system file browser with the file selected.
 * Equivalent to "View in Explorer" on Windows or "Show in Finder" on macOS.
 * @param filename The file to select
 */
void ShowInFileBrowser(const std::string& filename);

} // namespace Common
