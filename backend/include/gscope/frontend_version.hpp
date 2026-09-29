// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: a cheap "has the deployed frontend build changed?" signal.
// Metadata only (name/size/mtime per file), never file contents -- this
// runs on every broadcast, so it has to stay a handful of stat() calls,
// not a hash of the actual bytes.
#pragma once

#include <string>

namespace gscope {

// Fingerprints the immediate files of `directory` (non-recursive) by
// name+size+mtime. Deterministic regardless of directory-iteration order.
// Returns "0" for a missing/empty directory, so a backend started before
// the frontend is deployed still has a stable (if unhelpful) value rather
// than an empty string.
std::string computeDirectoryFingerprint(const std::string& directory);

}  // namespace gscope
