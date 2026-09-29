// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/frontend_version.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <system_error>
#include <vector>

namespace gscope {

std::string computeDirectoryFingerprint(const std::string& directory)
{
    namespace fs = std::filesystem;

    std::error_code errorCode;
    if (!fs::exists(directory, errorCode) || errorCode)
        return "0";

    struct Entry
    {
        std::string name;
        uintmax_t size = 0;
        fs::file_time_type mtime{};
    };
    std::vector<Entry> entries;

    for (auto it = fs::directory_iterator(directory, errorCode); !errorCode && it != fs::directory_iterator();
         it.increment(errorCode)) {
        std::error_code fileError;
        if (!it->is_regular_file(fileError) || fileError)
            continue;
        Entry entry;
        entry.name = it->path().filename().string();
        entry.size = it->file_size(fileError);
        entry.mtime = it->last_write_time(fileError);
        entries.push_back(std::move(entry));
    }
    if (entries.empty())
        return "0";

    // directory_iterator order isn't guaranteed stable across calls, so
    // the fingerprint has to sort first or two identical directories
    // could hash differently.
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });

    size_t combined = 0;
    for (const Entry& entry : entries) {
        combined ^= std::hash<std::string>{}(entry.name) + 0x9e3779b9 + (combined << 6) + (combined >> 2);
        combined ^= std::hash<uintmax_t>{}(entry.size) + 0x9e3779b9 + (combined << 6) + (combined >> 2);
        combined ^= std::hash<fs::file_time_type::rep>{}(entry.mtime.time_since_epoch().count()) +
                    0x9e3779b9 + (combined << 6) + (combined >> 2);
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%016zx", combined);
    return buffer;
}

}  // namespace gscope
