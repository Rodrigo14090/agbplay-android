#pragma once
#include <QString>
#include <filesystem>
#include <vector>

namespace DocumentAccess {
// Converts an Android SAF grant into a bounded app-private file for std::ifstream/libzip.
// Native paths are returned unchanged. The caller never needs broad storage permission.
std::filesystem::path LocalCopy(const QString &document, const QString &displayName = {});
struct FolderDocument { QString uri; QString name; };
// Only direct GSF children of the selected OST folder. Call from an import worker.
std::vector<FolderDocument> ListGsfFolder(const QString &directory);
}
namespace DocumentAccess {
bool ShareFile(const QString &path);
}
