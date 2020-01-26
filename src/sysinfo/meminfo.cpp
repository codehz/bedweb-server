#include "meminfo.h"
#include <sys/sysinfo.h>

namespace sys {

std::optional<struct sysinfo> getsysinfo() {
  struct sysinfo info = {};
  if (sysinfo(&info) == 0) return info;
  return {};
}

} // namespace sys