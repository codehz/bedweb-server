#pragma once

#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index_container_fwd.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <rpc.hpp>
#include <set>

#include "terminal_manager.hpp"

struct binary_handler : rpc::RPC::callback, terminal_manager::callback {
  using client_handler = rpc::RPC::client_handler;
  using term_id        = terminal_manager::ID;

  void on_remove(client_handler) override;
  void on_binary(client_handler, std::string_view data) override;

  void on_data(term_id, std::string_view) override;
  void on_close(term_id) override;

  std::string get(client_handler, uint32_t);
  void link_terminal(client_handler, term_id);
  void unlink_terminal(client_handler, term_id);
  bool check_terminal_link(client_handler, term_id);
  std::set<term_id> get_orphan_term();

private:
  std::map<client_handler, std::map<uint32_t, std::string>> bincache;
  struct terminfo {
    term_id id;
    client_handler handler;
  };
  using termset_t = boost::multi_index_container<
      terminfo, boost::multi_index::indexed_by<
                    boost::multi_index::ordered_unique<
                        boost::multi_index::tag<term_id>, boost::multi_index::member<terminfo, term_id, &terminfo::id>>,
                    boost::multi_index::ordered_non_unique<
                        boost::multi_index::tag<client_handler>,
                        boost::multi_index::member<terminfo, client_handler, &terminfo::handler>>>>;
  termset_t termset;
  std::set<term_id> orphan_term;
};