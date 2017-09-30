#ifndef SCHMAL_H
#define SCHMAL_H

#include <map>
#include <vector>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <memory>
#include <utility>
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>

using asio::ip::tcp;

#include "awaitio.h"

using namespace std;
using namespace awaitio;

namespace schmal {
  struct config_t;
  struct request {
    string method;
    string url;
    string scheme;
    string body;
    map<string, string> headers;
    map<string, string> cookies;
    bool api;
  };
  struct response
  {
    int status;
    map<string, string> headers;
    void add_header(string&, string&);
    string body;
  };
  struct parser;
  struct file_cache;
  struct acceptor_t;
  struct request_handler;

  template <typename socket_t>
  struct server
  {
  public:
    server(){}
    server(const server&) = delete;
    server(server&&) = delete;
    auto create();
    auto start();
  private:
    bool load_config();
    bool load_cache();
    config_t* cfg;
  };

  task start();
  void run();
}

#endif //SCHMAL_H
