#pragma once

#include <cstdint>
#include <rpc.hpp>
#include <map>

struct binary_record : rpc::RPC::callback {
  void on_remove(rpc::RPC::client_handler) override;
  void on_binary(rpc::RPC::client_handler, std::string_view data) override;

  std::map<rpc::RPC::client_handler, std::map<uint32_t, std::string>> bincache;
};