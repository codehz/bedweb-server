#pragma once
#include "binary_record.hpp"
#include <epoll.hpp>
#include <memory>
#include <rpc.hpp>
#include <variant>

struct api_config {
  unsigned perid;
  std::string monitor_path;
};

void prepare(rpc::RPC &rpc, std::shared_ptr<binary_record> binrecord, std::shared_ptr<epoll> ep, api_config const &config);
