#pragma once

#include <optional>
#include <rpc.hpp>
#include <sys/sysinfo.h>

namespace sys {

std::optional<struct sysinfo> getsysinfo();

}

inline void to_json(rpc::json &j, const struct sysinfo &info) {
  j = rpc::json{
      {"uptime", info.uptime},       {"loads", {info.loads[0], info.loads[1], info.loads[2]}},
      {"totalram", info.totalram},   {"freeram", info.freeram},
      {"sharedram", info.sharedram}, {"bufferram", info.bufferram},
      {"totalswap", info.totalswap}, {"freeswap", info.freeswap},
      {"procs", info.procs},         {"totalhigh", info.totalhigh},
      {"freehigh", info.freehigh},   {"mem_unit", info.mem_unit},
  };
}