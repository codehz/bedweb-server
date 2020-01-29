#include <epoll.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <ostream>
#include <rpc.hpp>
#include <signal.h>
#include <stdexcept>
#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/mark.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/yaml.h>

#define OPENSSL_ENABLED 1
#include <rpcws.hpp>

#include "api.hpp"
#include "binary_handler.hpp"

struct CheckFailed : std::runtime_error {
  YAML::Mark mark;
  CheckFailed(YAML::Mark mark, std::string msg) : runtime_error(std::move(msg)), mark(mark) {}
};

template <typename T> T check(YAML::Node const &node, std::string name) {
  if (auto sub = node[name]; sub) {
    try {
      return sub.as<T>();
    } catch (YAML::BadConversion const &) {
      throw CheckFailed{node.Mark(), "Attribute '" + name + "' failed to convert to " + typeid(T).name() + "."};
    }
  } else
    throw CheckFailed{node.Mark(), "Attribute '" + name + "' is required."};
}

int main() {
  using namespace rpcws;
  try {
    signal(SIGPIPE, SIG_IGN);
    YAML::Node config = YAML::LoadFile("bedweb.yaml");
    api_config apicfg;
    std::unique_ptr<ssl_context> ssl;
    if (auto sslcfg = config["ssl"]; sslcfg && sslcfg.IsMap()) {
      auto cert = check<std::string>(sslcfg, "cert");
      auto priv = check<std::string>(sslcfg, "priv");
      ssl       = std::make_unique<ssl_context>(cert, priv);
    }
    auto address        = check<std::string>(config, "listen");
    apicfg.perid        = config["qeury_period"].as<unsigned>(1);
    apicfg.monitor_path = config["monitor_path"].as<std::string>("/");
    auto ep             = std::make_shared<epoll>();
    std::unique_ptr<server_wsio> wsio;
    if (ssl)
      wsio = std::make_unique<server_wsio>(std::move(ssl), address, ep);
    else
      wsio = std::make_unique<server_wsio>(address, ep);
    auto binrecord = std::make_shared<binary_handler>();
    RPC server(std::move(wsio), binrecord);

    prepare(server, binrecord, ep, apicfg);

    server.start();
    ep->wait();
    std::cerr << "done" << std::endl;
  } catch (YAML::BadFile const &) {
    std::cerr << "Failed to load config bedweb.yaml: file not found" << std::endl;
  } catch (CheckFailed const &bad) {
    std::cerr << "Failed to load config bedweb.yaml: " << bad.what() << std::endl;
    if (auto &mark = bad.mark; !mark.is_null())
      std::cerr << "Line: " << mark.line << ", column: " << mark.column << ", pos: " << mark.pos << std::endl;
  } catch (SSLError const &sslerr) { std::cerr << "SSLError: " << sslerr.what() << std::endl; }
}