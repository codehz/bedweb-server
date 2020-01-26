#pragma once

#include <filesystem>
#include <optional>
#include <rpc.hpp>

namespace sys {

std::filesystem::space_info getDiskSize(std::filesystem::path const &file);

}

namespace std::filesystem {
inline void to_json(rpc::json &j, const space_info &space) {
  j = rpc::json{{"capacity", space.capacity}, {"free", space.free}, {"available", space.available}};
}
} // namespace std::filesystem