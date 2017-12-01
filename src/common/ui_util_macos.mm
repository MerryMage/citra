// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#import <Cocoa/Cocoa.h>

#include <string>
#include "common/ui_util.h"

namespace Common {

void ShowInFileBrowser(const std::string& filename) {
    if (NSString* ns_filename = @(filename.c_str())) {
        [[NSWorkspace sharedWorkspace] selectFile:ns_filename inFileViewerRootedAtPath:@""];
    }
}

} // namespace Common
