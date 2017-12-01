// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <windows.h>
// windows.h must come first
#include <objbase.h>
#include <shlobj.h>

#include <string>
#include "common/ui_util.h"

namespace Common {

void ShowInFileBrowser(const std::string& filename) {
    CoInitialize(nullptr);
    if (ITEMIDLIST* pidl = ILCreateFromPath(filename.c_str())) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
    CoUninitialize();
}

} // namespace Common
