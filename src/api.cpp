#include "api.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json.hpp>
#include <limits>
#include <random>
#include <rpc.hpp>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include "sysinfo/cpuinfo.h"
#include "sysinfo/diskspace.h"
#include "sysinfo/meminfo.h"

using namespace rpc;
namespace fs                            = std::filesystem;
constexpr inline auto max_binary_packet = 16384;

struct syserror : std::runtime_error {
  syserror(std::string content) : runtime_error(content + ":" + strerror(errno)) {}
};

inline static json build_cpustat(sys::CPU const &cpuinfo) {
  return json({
      {"global", cpuinfo.getGlobalStat()},
      {"separated", cpuinfo.getStats()},
      {"time", time(nullptr)},
  });
}

template <typename Callback> class Timer {
  int timerfd;
  std::shared_ptr<epoll> epfd;

public:
  Timer(unsigned sec, std::shared_ptr<epoll> ep, Callback callback) : epfd(std::move(ep)) {
    timerfd                  = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    itimerspec timer         = {};
    timer.it_value.tv_sec    = sec;
    timer.it_interval.tv_sec = sec;
    timerfd_settime(timerfd, 0, &timer, nullptr);

    epfd->add(EPOLLIN | EPOLLERR, timerfd, epfd->reg([this, callback](const epoll_event &ev) {
      if (ev.events & EPOLLERR) {
        epfd->del(timerfd);
      } else if (ev.events & EPOLLIN) {
        char buffer[8];
        read(timerfd, buffer, 8);
        callback();
      }
    }));
  }

  ~Timer() {
    epfd->del(timerfd);
    close(timerfd);
  }
};

template <typename Callback> Timer(int, std::shared_ptr<epoll>, Callback)->Timer<Callback>;

// Fix this for gcc 9.2.0
struct hack_clock : fs::file_time_type::clock {
  template <typename TargetDur, typename _Dur>
  static std::chrono::time_point<fs::file_time_type::clock, TargetDur>
  from_sys(const std::chrono::time_point<std::chrono::system_clock, _Dur> &__t) noexcept {
    return std::chrono::time_point_cast<TargetDur>(_S_from_sys(__t));
  }

  template <typename TargetDur, typename _Dur>
  static std::chrono::time_point<std::chrono::system_clock, TargetDur>
  to_sys(const std::chrono::time_point<fs::file_time_type::clock, _Dur> &__t) noexcept {
    return std::chrono::time_point_cast<TargetDur>(_S_to_sys(__t));
  }
};

namespace nlohmann {
template <typename T> struct adl_serializer<std::optional<T>> {
  inline static void to_json(rpc::json &j, const std::optional<T> &opt) {
    if (opt.has_value())
      ::to_json(j, opt.value());
    else
      j = nullptr;
  }
};
template <> struct adl_serializer<fs::directory_entry> {
  inline static void to_json(rpc::json &j, const fs::directory_entry &entry) {
    j = json::object({
        {"name", entry.path().filename().c_str()},
        {"type", entry.status().type()},
        {"perm", entry.status().permissions()},
        {"link", entry.hard_link_count()},
        {"time", (unsigned long) hack_clock::to_sys<std::chrono::milliseconds>(entry.last_write_time())
                     .time_since_epoch()
                     .count()},
    });
  }
};

template <> struct adl_serializer<fs::file_status> {
  inline static void to_json(rpc::json &j, const fs::file_status &status) {
    j = json::object({
        {"type", status.type()},
        {"perm", status.permissions()},
    });
  }
};
// struct stat
} // namespace nlohmann

uint32_t gen_blob_id() {
  static std::random_device rd;
  static std::default_random_engine e{rd()};
  static std::uniform_int_distribution<uint32_t> dist(std::numeric_limits<uint32_t>::min());
  return dist(e);
}

void prepare(
    RPC &server, std::shared_ptr<binary_record> binrecord, std::shared_ptr<epoll> ep, api_config const &config) {
  server.reg("ping", [](auto client, json input) -> json { return "pong"; });

  static sys::CPU cpuinfo{};
  server.event("sysinfo.cpustat");
  server.reg("sysinfo.cpuid", [&](auto client, json input) -> json { return cpuinfo.getCPUID(); });
  server.reg("sysinfo.cpustat", [&](auto client, json input) -> json { return build_cpustat(cpuinfo); });

  server.event("sysinfo.sysinfo");
  server.reg("sysinfo.sysinfo", [&](auto client, json input) -> json { return sys::getsysinfo(); });

  server.event("sysinfo.diskspace");
  server.reg("sysinfo.diskspace", [&](auto client, json input) -> json {
    auto path = (input.size() == 1 && input[0].is_string()) ? input[0].get<std::string>() : config.monitor_path;
    return sys::getDiskSize(path);
  });

  auto callback = [&] {
    cpuinfo.snapshot();
    server.emit("sysinfo.cpustat", build_cpustat(cpuinfo));
    server.emit("sysinfo.sysinfo", sys::getsysinfo());
    server.emit("sysinfo.diskspace", {{"path", config.monitor_path}, {"info", sys::getDiskSize(config.monitor_path)}});
  };
  static auto timer = Timer{config.perid ?: 1, std::move(ep), callback};

  server.reg("fs.ls", [&](auto client, json input) -> json {
    auto path = input[0].get<std::string>();
    auto ret  = json::array();
    for (const auto &entry : fs::directory_iterator{path, fs::directory_options::skip_permission_denied})
      ret.push_back(entry);
    return ret;
  });
  server.reg("fs.tree", [&](auto client, json input) -> json {
    auto path = input[0].get<std::string>();
    auto ret  = json::array();
    for (const auto &entry : fs::recursive_directory_iterator{path, fs::directory_options::skip_permission_denied})
      ret.push_back(entry);
    return ret;
  });
  server.reg("fs.pread", [&](std::shared_ptr<server_io::client> client, json input) -> json {
    auto path   = input[0].get<std::string>();
    auto offset = input[1].get<size_t>();
    auto size   = input[2].get<size_t>();
    if (size > max_binary_packet || size == 0) throw std::length_error("size");
    int file = open(path.c_str(), 0);
    static char shared_buffer[max_binary_packet + 4];
    auto ret = pread(file, shared_buffer + 4, size, offset);
    if (ret == -1) throw syserror("pread");
    if (ret == 0) return json::object({{"blob", nullptr}});
    auto id = gen_blob_id();
    memcpy(shared_buffer, &id, sizeof id);
    client->send({shared_buffer, size + 4}, message_type::BINARY);
    return json::object({{"blob", ntohs(id)}});
  });
  server.reg("fs.pwrite", [&, binrecord](std::shared_ptr<server_io::client> client, json input) -> json {
    auto path   = input[0].get<std::string>();
    auto offset = input[1].get<size_t>();
    auto blob   = input[2].get<uint32_t>();
    auto &cache = binrecord->bincache[client];
    auto it     = cache.find(blob);
    if (it == cache.end()) throw std::invalid_argument("blob not found");
    std::string data{std::move(it->second)};
    cache.erase(it);
    int file = open(path.c_str(), 0);
    auto ret = pwrite(file, data.c_str(), data.size(), offset);
    if (ret == -1) throw syserror("pwrite");
    return ret;
  });
  server.reg("fs.copy", [&](auto client, json input) -> json {
    auto path   = input[0].get<std::string>();
    auto target = input[1].get<std::string>();
    fs::copy_options options;
    if (input.size() == 3) {
      auto &opt = input[2];
      if (opt["skip_existing"].get<bool>()) options |= fs::copy_options::skip_existing;
      if (opt["overwrite_existing"].get<bool>()) options |= fs::copy_options::overwrite_existing;
      if (opt["update_existing"].get<bool>()) options |= fs::copy_options::update_existing;
      if (opt["recursive"].get<bool>()) options |= fs::copy_options::recursive;
      if (opt["copy_symlinks"].get<bool>()) options |= fs::copy_options::copy_symlinks;
      if (opt["skip_symlinks"].get<bool>()) options |= fs::copy_options::skip_symlinks;
      if (opt["directories_only"].get<bool>()) options |= fs::copy_options::directories_only;
      if (opt["create_symlinks"].get<bool>()) options |= fs::copy_options::create_symlinks;
      if (opt["create_hard_links"].get<bool>()) options |= fs::copy_options::create_hard_links;
    }
    fs::copy(target, path, options);
    return nullptr;
  });
  server.reg("fs.symlink", [&](auto client, json input) -> json {
    auto path   = input[0].get<std::string>();
    auto target = input[1].get<std::string>();
    fs::create_symlink(target, path);
    return nullptr;
  });
  server.reg("fs.hardlink", [&](auto client, json input) -> json {
    auto path   = input[0].get<std::string>();
    auto target = input[1].get<std::string>();
    fs::create_hard_link(target, path);
    return nullptr;
  });
  server.reg("fs.mkdir", [&](auto client, json input) -> json {
    auto path = input[0].get<std::string>();
    return fs::create_directories(path);
  });
  server.reg("fs.realpath", [&](auto client, json input) -> json {
    auto path = input[0].get<std::string>();
    return fs::canonical(path);
  });
  server.reg("fs.resize", [&](auto client, json input) -> json {
    auto path     = input[0].get<std::string>();
    auto new_size = input[0].get<std::uintmax_t>();
    fs::resize_file(path, new_size);
    return nullptr;
  });
  server.reg("fs.remove", [&](auto client, json input) -> json {
    auto path = input[0].get<std::string>();
    return fs::remove_all(path);
  });
  server.reg("fs.exists", [&](auto client, json input) -> json {
    auto path = input[0].get<std::string>();
    return fs::exists(path);
  });
  server.reg("fs.stat", [&](auto client, json input) -> json {
    auto path = input[0].get<std::string>();
    return fs::status(path);
  });
  server.reg("fs.lstat", [&](auto client, json input) -> json {
    auto path = input[0].get<std::string>();
    return fs::symlink_status(path);
  });

  std::cerr << "init finished" << std::endl;
}