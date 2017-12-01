// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>
#include "common/string_util.h"
#include "common/ui_util.h"

namespace Common {

void ShowInFileBrowser(const std::string& filename) {
    std::string path;
    if (!Common::SplitPath(filename, &path, nullptr, nullptr))
        return;
    path = Common::ReplaceAll(path, "\\", "\\\\");
    path = Common::ReplaceAll(path, "\"", "\\\"");
    system(("xdg-open \"" + path + '"').c_str());
}

} // namespace Common
