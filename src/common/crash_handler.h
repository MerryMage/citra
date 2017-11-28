// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <functional>
#include <string>
#include <vector>
#include <boost/optional.hpp>

namespace Common {

struct CrashInformation {
    std::vector<std::string> stack_trace;
    boost::optional<std::string> minidump_filename;
};

void CrashHandler(std::function<void()> try_, std::function<void(const CrashInformation&)> catch_,
                  boost::optional<std::string> minidump_filename = {});

} // namespace Common
