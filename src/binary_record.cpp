#include "binary_record.hpp"
#include <cstdint>
#include <cstring>
#include <netinet/in.h>

void binary_record::on_remove(rpc::RPC::client_handler handler) { bincache.erase(handler); }

void binary_record::on_binary(rpc::RPC::client_handler handler, std::string_view data) {
  uint32_t id;
  std::memcpy(&id, data.data(), sizeof id);
  id = ntohl(id);
  data.remove_prefix(sizeof id);
  bincache[handler][id] = data;
}