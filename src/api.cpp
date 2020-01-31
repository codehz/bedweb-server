#include "api.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <json.hpp>
#include <limits>
#include <pwd.h>
#include <random>
#include <rpc.hpp>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include "syserror.hpp"
#include "sysinfo/cpuinfo.h"
#include "sysinfo/diskspace.h"
#include "sysinfo/meminfo.h"
#include "terminal_manager.hpp"
#include "timer.hpp"

using namespace rpc;
namespace fs                            = std::filesystem;
constexpr inline auto max_binary_packet = 16384;

inline static json build_cpustat(sys::CPU const &cpuinfo) {
  return json({
      {"global", cpuinfo.getGlobalStat()},
      {"separated", cpuinfo.getStats()},
      {"time", time(nullptr)},
  });
}

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
template <> struct adl_serializer<fs::file_type> {
  inline static void to_json(rpc::json &j, const fs::file_type &type) {
#define detect(name) type == fs::file_type::name ? #name:
    j = detect(block) detect(character) detect(directory) detect(fifo) detect(regular) detect(socket)
        detect(symlink) "unknown";
#undef detect
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
template <> struct adl_serializer<passwd> {
  inline static void to_json(rpc::json &j, const passwd &pd) {
    j = json::object({
        {"uid", pd.pw_uid},
        {"gid", pd.pw_gid},
        {"username", pd.pw_name},
        {"realneme", pd.pw_gecos},
        {"home", pd.pw_dir},
        {"shell", pd.pw_shell},
    });
  }
};
template <> struct adl_serializer<group> {
  inline static void to_json(rpc::json &j, const group &grp) {
    auto members = rpc::json::array();
    auto it      = grp.gr_mem;
    while (*it) {
      members.push_back(*it);
      it++;
    }
    j = json::object({
        {"gid", grp.gr_gid},
        {"name", grp.gr_name},
        {"members", members},
    });
  }
};
} // namespace nlohmann

uint32_t gen_blob_id(bool terminal = false) {
  static std::random_device rd;
  static std::default_random_engine e{rd()};
  static std::uniform_int_distribution<uint32_t> dist(
      std::numeric_limits<uint32_t>::min(), std::numeric_limits<uint32_t>::max() >> 1);
  return dist(e);
}

void prepare(
    RPC &server, std::shared_ptr<binary_handler> binhandler, std::shared_ptr<epoll> ep, api_config const &config) {
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

  server.reg("sysinfo.users", [&](auto client, json input) -> json {
    passwd *pd;
    auto ret = json::array();
    setpwent();
    while ((pd = getpwent())) ret.push_back(*pd);
    endpwent();
    return ret;
  });
  server.reg("sysinfo.groups", [&](auto client, json input) -> json {
    group *grp;
    auto ret = json::array();
    setgrent();
    while ((grp = getgrent())) ret.push_back(*grp);
    endgrent();
    return ret;
  });
  server.reg("sysinfo.current_user", [&](auto client, json input) -> json {
    gid_t list[getgroups(0, nullptr)];
    getgroups(sizeof(list) / sizeof(list[0]), list);
    auto groups = json::array();
    for (int i = 0; i < sizeof(list) / sizeof(list[0]); i++) groups.push_back(list[i]);
    return json::object({
        {"uid", getuid()},
        {"euid", geteuid()},
        {"gid", getgid()},
        {"egid", getegid()},
        {"groups", groups},
    });
  });

  auto callback = [&] {
    cpuinfo.snapshot();
    server.emit("sysinfo.cpustat", build_cpustat(cpuinfo));
    server.emit("sysinfo.sysinfo", sys::getsysinfo());
    server.emit("sysinfo.diskspace", {{"path", config.monitor_path}, {"info", sys::getDiskSize(config.monitor_path)}});
  };
  static auto timer = Timer{config.perid ?: 1, ep, callback};

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
  server.reg("fs.pwrite", [&, binhandler](std::shared_ptr<server_io::client> client, json input) -> json {
    auto path   = input[0].get<std::string>();
    auto offset = input[1].get<size_t>();
    auto blob   = input[2].get<uint32_t>();
    auto data   = binhandler->get(client, blob);
    int file    = open(path.c_str(), 0);
    auto ret    = pwrite(file, data.c_str(), data.size(), offset);
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

  static terminal_manager termmgr{binhandler, ep};
  server.reg("shell.open_shell", [&, binhandler](auto client, json input) -> json {
    auto shell = getenv("SHELL");
    if (!shell) throw std::runtime_error("no SHELL env");
    auto id = termmgr.alloc_terminal(shell, {"-l"});
    binhandler->link_terminal(client, id);
    return id;
  });
  server.reg("shell.open", [&, binhandler](auto client, json input) -> json {
    auto program = input[0].get<std::string>();
    auto args    = input[1].get<std::vector<std::string>>();
    auto id      = termmgr.alloc_terminal(program, args);
    binhandler->link_terminal(client, id);
    return id;
  });
  server.reg("shell.resize", [&, binhandler](auto client, json input) -> json {
    auto id  = input[0].get<std::uint32_t>();
    auto row = input[1].get<std::uint16_t>();
    auto col = input[2].get<std::uint16_t>();
    if (binhandler->check_terminal_link(client, id)) termmgr.resize_terminal(id, {row, col});
    return nullptr;
  });
  server.reg("shell.unlink", [&, binhandler](auto client, json input) -> json {
    auto id = input[0].get<binary_handler::term_id>();
    binhandler->unlink_terminal(client, id);
    return nullptr;
  });
  server.reg("shell.close", [&, binhandler](auto client, json input) -> json {
    auto id = input[0].get<binary_handler::term_id>();
    if (binhandler->check_terminal_link(client, id)) termmgr.close(id);
    return nullptr;
  });

  std::cerr << "init finished" << std::endl;
}