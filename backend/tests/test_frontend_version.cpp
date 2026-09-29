// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/frontend_version.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

void writeFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream file(path, std::ios::binary);
    file << content;
}
}  // namespace

int main()
{
    namespace fs = std::filesystem;

    check(gscope::computeDirectoryFingerprint("/does/not/exist") == "0", "missing directory -> \"0\"");

    fs::path dir = fs::temp_directory_path() / "gscope_frontend_version_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    check(gscope::computeDirectoryFingerprint(dir.string()) == "0", "empty directory -> \"0\"");

    writeFile(dir / "index.html", "<html></html>");
    writeFile(dir / "main.js", "console.log(1)");
    std::string first = gscope::computeDirectoryFingerprint(dir.string());
    check(first != "0", "non-empty directory produces a real fingerprint");

    std::string again = gscope::computeDirectoryFingerprint(dir.string());
    check(first == again, "unchanged directory -> stable fingerprint");

    // Give the new write a distinct mtime from the original -- some
    // filesystems have coarse (1s) mtime resolution.
    writeFile(dir / "main.js", "console.log(1); console.log(2);");
    fs::last_write_time(dir / "main.js", fs::file_time_type::clock::now() + std::chrono::seconds(2));
    std::string afterEdit = gscope::computeDirectoryFingerprint(dir.string());
    check(afterEdit != first, "editing a file changes the fingerprint");

    writeFile(dir / "extra.css", "body{}");
    std::string afterAdd = gscope::computeDirectoryFingerprint(dir.string());
    check(afterAdd != afterEdit, "adding a file changes the fingerprint");

    fs::remove_all(dir);
    std::puts("frontend version: directory fingerprint changes with content, stable otherwise");
    return 0;
}
