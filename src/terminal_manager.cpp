#include "terminal_manager.hpp"
#include <csignal>
#include <cstring>
#include <netinet/in.h>
#include <pty.h>
#include <signal.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

terminal_manager::terminal_manager(std::shared_ptr<callback> cb, std::shared_ptr<epoll> ep)
    : callback_ref(std::move(cb)), ep(ep) {
  chld     = ep->reg([this](const epoll_event &ev) {
    if (ev.events & EPOLLIN) {
      signalfd_siginfo info;
      read(sigfd, &info, sizeof info);
      auto pid = wait(nullptr);
      auto it  = pidset.get<pid_t>().find(pid);
      auto id  = it->id;
      this->ep->del(id);
      callback_ref->on_close(id);
      pidset.erase(it);
    } else {
      this->ep->del(sigfd);
      close(sigfd);
    }
  });
  pty_read = ep->reg([this](const epoll_event &ev) {
    if (ev.events & EPOLLIN) {
      static char shared_buffer[32767 + sizeof(ID)];
      auto len = read(ev.data.fd, shared_buffer + sizeof(ID), sizeof shared_buffer - sizeof(ID));
      ID nid   = htonl(ev.data.fd);
      std::memcpy(&nid, shared_buffer, sizeof nid);
      if (len == -1) return;
      callback_ref->on_data(ev.data.fd, {shared_buffer, len + sizeof nid});
    } else {
      this->ep->del(ev.data.fd);
      close(ev.data.fd);
    }
  });

  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGCHLD);
  sigprocmask(SIG_BLOCK, &sigset, NULL);
  int sfd = signalfd(-1, &sigset, SFD_NONBLOCK | SFD_CLOEXEC);
  ep->add(EPOLLIN | EPOLLERR, sfd, chld);
}

const constexpr static winsize default_winsize = {80, 25};

terminal_manager::ID
terminal_manager::alloc_terminal(std::string const &program, std::vector<std::string> const &args) {
  int master;
  int ret = forkpty(&master, nullptr, nullptr, &default_winsize);
  if (ret == 0) {
    prctl(PR_GET_PDEATHSIG, SIGHUP);
    auto argv = new char const *[args.size() + 1];
    auto it   = argv;
    for (auto &str : args) *it++ = str.c_str();
    *it = nullptr;
    exit(execvp(program.c_str(), (char **) argv));
  }
  pidset.insert({ret, (ID) master});
  ep->add(EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP, master, pty_read);
  return master;
}

void terminal_manager::resize_terminal(terminal_manager::ID id, winsize size) {
  if (pidset.get<ID>().count(id) == 0) throw std::invalid_argument("id not found");
  ioctl(id, TIOCSWINSZ, &size);
}

void terminal_manager::send_data(terminal_manager::ID id, std::string_view data) {
  if (pidset.get<ID>().count(id) == 0) throw std::invalid_argument("id not found");
  write(id, data.data(), data.size());
}

void terminal_manager::close(terminal_manager::ID id) {
  auto it = pidset.get<ID>().find(id);
  if (it == pidset.get<ID>().end()) throw std::invalid_argument("id not found");
  pidset.get<ID>().erase(it);
  ep->del(id);
  callback_ref->on_close(id);
  close(id);
}