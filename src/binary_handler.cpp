#include "binary_handler.hpp"
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <rpc.hpp>
#include <tuple>
#include <unistd.h>

static constexpr uint32_t magic = (1ul << 31);

bool binary_handler::check_terminal_link(client_handler handler, terminal_manager::ID id) {
  auto it = termset.get<client_handler>().find(handler);
  return it != termset.get<client_handler>().end();
}

void binary_handler::on_remove(client_handler handler) {
  bincache.erase(handler);
  auto &hset = termset.get<client_handler>();
  auto it    = hset.lower_bound(handler);
  auto end   = hset.upper_bound(handler);
  while (it != end) {
    orphan_term.insert(it->id);
    it = hset.erase(it);
  }
}

void binary_handler::on_binary(client_handler handler, std::string_view data) {
  uint32_t id;
  std::memcpy(&id, data.data(), sizeof id);
  id = ntohl(id);
  data.remove_prefix(sizeof id);
  if (id >= magic) {
    id -= magic;
    if (check_terminal_link(handler, id)) { write(id, data.data(), data.size()); }
  } else {
    bincache[handler][id] = data;
  }
}

void binary_handler::on_data(term_id id, std::string_view data) {
  auto it = termset.get<term_id>().find(id);
  if (it == termset.get<term_id>().end()) return;
  it->handler->send(data, rpc::message_type::BINARY);
}

void binary_handler::on_close(term_id id) {
  auto it = termset.get<term_id>().find(id);
  if (it == termset.get<term_id>().end()) return;
  union {
    uint32_t id;
    char buf[sizeof(uint32_t)];
  } u;
  u.id = htonl(id);
  it->handler->send({u.buf, sizeof(uint32_t)}, rpc::message_type::BINARY);
  termset.get<term_id>().erase(it);
  orphan_term.erase(it->id);
}

std::string binary_handler::get(client_handler handler, uint32_t id) {
  auto &cache = this->bincache[handler];
  auto it     = cache.find(id);
  if (it == cache.end()) throw std::invalid_argument("blob not found");
  std::string data{std::move(it->second)};
  cache.erase(it);
  return data;
}

void binary_handler::link_terminal(client_handler handler, terminal_manager::ID id) {
  termset.insert({id, std::move(handler)});
}

void binary_handler::unlink_terminal(client_handler handler, terminal_manager::ID id) {
  termset.get<term_id>().erase(id);
}