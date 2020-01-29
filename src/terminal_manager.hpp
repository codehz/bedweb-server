#pragma once

#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index_container.hpp>
#include <cstdint>
#include <epoll.hpp>
#include <map>
#include <memory>
#include <pty.h>
#include <sched.h>
#include <set>
#include <string_view>

class terminal_manager {
public:
  using ID = uint32_t;
  struct callback {
    virtual void on_data(ID, std::string_view) = 0;
    virtual void on_close(ID)                  = 0;
  };

private:
  std::shared_ptr<callback> callback_ref;
  struct pidpair {
    pid_t pid;
    ID id;
  };
  boost::multi_index_container<
      pidpair, boost::multi_index::indexed_by<
                   boost::multi_index::ordered_unique<
                       boost::multi_index::tag<pid_t>, boost::multi_index::member<pidpair, pid_t, &pidpair::pid>>,
                   boost::multi_index::ordered_unique<
                       boost::multi_index::tag<ID>, boost::multi_index::member<pidpair, ID, &pidpair::id>>>>
      pidset;
  std::shared_ptr<epoll> ep;
  int sigfd, chld, pty_read;

public:
  terminal_manager(std::shared_ptr<callback> cb, std::shared_ptr<epoll> ep);
  ID alloc_terminal(std::string const &program, std::vector<std::string> const &args);
  void resize_terminal(ID id, winsize size);
  void send_data(ID id, std::string_view data);
  void close(ID id);
};