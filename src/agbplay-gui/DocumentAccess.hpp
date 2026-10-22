#pragma once
#include <QString>
#include <filesystem>

namespace DocumentAccess {
// Converts an Android SAF grant into a bounded app-private file for std::ifstream/libzip.
// Native paths are returned unchanged. The caller never needs broad storage permission.
std::filesystem::path LocalCopy(const QString &document);
}
