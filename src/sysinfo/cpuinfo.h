#pragma once

#include <epoll.hpp>
#include <fstream>
#include <functional>
#include <libcpuid/libcpuid.h>
#include <memory>
#include <optional>
#include <rpc.hpp>
#include <vector>

namespace sys {

class CPU {
public:
  struct cpu_stat {
    u_int32_t user;    /* Time spent in user mode */
    u_int32_t nice;    /* Time spent in user mode with low priority (nice) */
    u_int32_t systm;   /* Time spent in system mode */
    u_int32_t idle;    /* Time spent in the idle task */
    u_int32_t iowait;  /* Time waiting for I/O to complete */
    u_int32_t irq;     /* Time servicing interrupts */
    u_int32_t softirq; /* Time servicing softirqs */
    u_int32_t steal;   /* Stolen time, which is the time spent in other operating systems when running in a virtualized
                          environment */
    u_int32_t
        guest; /* Time spent running a virtual CPU for guest operating systems under the control of the Linux kernel */
    u_int32_t guest_nice; /* Time spent running a niced guest */
  };

private:
  std::ifstream stat;
  std::optional<cpu_id_t> cpuid;
  cpu_stat global_stat;
  std::vector<cpu_stat> stats;

public:
  CPU();
  ~CPU();
  void snapshot();
  inline std::optional<cpu_id_t> const &getCPUID() const { return cpuid; }
  inline cpu_stat const &getGlobalStat() const { return global_stat; }
  inline std::vector<cpu_stat> const &getStats() const { return stats; }
};
} // namespace sys

namespace nlohmann {
template <> struct adl_serializer<sys::CPU::cpu_stat> {
  inline static void to_json(rpc::json &j, const sys::CPU::cpu_stat &stat) {
    j = rpc::json{
        {"user", stat.user},       {"nice", stat.nice},
        {"systm", stat.systm},     {"idle", stat.idle},
        {"iowait", stat.iowait},   {"irq", stat.irq},
        {"softirq", stat.softirq}, {"steal", stat.steal},
        {"guest", stat.guest},     {"guest_nice", stat.guest_nice},
    };
  }
};
} // namespace nlohmann

inline void to_json(rpc::json &j, const cpu_id_t &cpuid) {
  j = rpc::json{
      {"vendor", cpuid.vendor_str},
      {"brand", cpuid.brand_str},
      {"codename", cpuid.cpu_codename},
      {"family", cpuid.family},
      {"model", cpuid.model},
      {"stepping", cpuid.stepping},
      {"ext_family", cpuid.ext_family},
      {"ext_model", cpuid.ext_model},
      {"cores", cpuid.num_cores},
      {"logical_cores", cpuid.num_logical_cpus},
      {"total_logical_cores", cpuid.total_logical_cpus},
      {"l1_data_cache", cpuid.l1_data_cache},
      {"l1_instruction_cache", cpuid.l1_instruction_cache},
      {"l2_cache", cpuid.l2_cache},
      {"l3_cache", cpuid.l3_cache},
      {"l4_cache", cpuid.l4_cache},
      {"l1_assoc", cpuid.l1_assoc},
      {"l2_assoc", cpuid.l2_assoc},
      {"l3_assoc", cpuid.l3_assoc},
      {"l4_assoc", cpuid.l4_assoc},
      {"l1_cacheline", cpuid.l1_cacheline},
      {"l2_cacheline", cpuid.l2_cacheline},
      {"l3_cacheline", cpuid.l3_cacheline},
      {"l4_cacheline", cpuid.l4_cacheline},
      {"sse_size", cpuid.sse_size},
  };
}