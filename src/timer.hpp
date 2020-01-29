#pragma once

#include <epoll.hpp>
#include <memory>
#include <sys/timerfd.h>

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