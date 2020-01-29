#pragma once

#include <cstring>
#include <errno.h>
#include <stdexcept>
#include <string>

struct syserror : std::runtime_error {
  syserror(std::string content) : runtime_error(content + ":" + strerror(errno)) {}
};