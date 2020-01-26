#include "cpuinfo.h"
#include <ctime>
#include <fstream>
#include <iostream>
#include <libcpuid/libcpuid.h>
#include <optional>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace sys {

static std::optional<cpu_id_t> get_cpu_id() {
  cpu_raw_data_t raw;
  cpu_id_t cpuid;
  if (!cpuid_present()) {
    std::cerr << "Failed to get cpu info: NO CPUID" << std::endl;
    return {};
  }
  if (cpuid_get_raw_data(&raw) < 0) {
    std::cerr << "Failed to get cpu info: " << cpuid_error() << std::endl;
    return {};
  }
  if (cpu_identify(&raw, &cpuid) < 0) {
    std::cerr << "Failed to identify cpu info: " << cpuid_error() << std::endl;
    return {};
  }
  return cpuid;
}

CPU::CPU() {
  cpuid   = get_cpu_id();
  stat    = std::ifstream{"/proc/stat"};
  
  // timerfd_settime(timerfd, 0, &timer, nullptr);
  // epfd->add(EPOLLIN | EPOLLERR, timerfd, epfd->reg([this, callback](const epoll_event &ev) {
  //   if (ev.events & EPOLLERR) {
  //     epfd->del(timerfd);
  //     // TODO: report error;
  //   } else if (ev.events & EPOLLIN) {
  //     char buffer[8];
  //     read(timerfd, buffer, 8);
  //     snapshot();
  //     callback();
  //   }
  // }));
  snapshot();
}

CPU::~CPU() {
}

void CPU::snapshot() {
  stat.seekg(0);
  int cpuindex = -1;
  while (1) {
    std::string key;
    cpu_stat cstat = {};
    stat >> key;
    if (!key.compare(0, 3, "cpu")) {
      stat >> cstat.user;
      stat >> cstat.nice;
      stat >> cstat.systm;
      stat >> cstat.idle;
      stat >> cstat.iowait;
      stat >> cstat.irq;
      stat >> cstat.softirq;
      stat >> cstat.steal;
      stat >> cstat.guest;
      stat >> cstat.guest_nice;
      if (!stat) {
        std::cerr << "Warning: Failed to parse /proc/stat" << std::endl;
        std::cerr.clear();
        return;
      }
      if (cpuindex == -1) {
        global_stat = cstat;
      } else {
        if (stats.size() <= cpuindex) stats.push_back(cstat);
        else stats[cpuindex] = cstat;
      }
      cpuindex++;
    } else break;
  }
  stats.resize(cpuindex);
}

} // namespace sysinfo