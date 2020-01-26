#include "diskspace.h"
#include <filesystem>

namespace sys {

std::filesystem::space_info getDiskSize(std::filesystem::path const &path) {
  return std::filesystem::space(path);
}

} // namespace sys
