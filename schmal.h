#ifndef SCHMAL_H
#define SCHMAL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#define getcwd _getcwd
#else
#define closesocket close
#endif


#include <memory>
#include <iostream>
#include <map>
#include <vector>

#include "awaitio.h"

using namespace std;
using namespace awaitio;

namespace schmal {

  struct config_t;
  struct io_buff_t;

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
  struct stream_t {
    virtual ~stream_t() {}
    virtual void read(io_buff_t&) = 0;
    virtual int write(char*, size_t) = 0;
    virtual int close() = 0;
  };
  struct socket_t {
    virtual ~socket_t() {}
    virtual int create();
    virtual int bind(string&, short);
    virtual int listen();
    virtual int set_non_blocking(bool);
    virtual int set_tcp_no_delay(bool);
    virtual int set_tcp_keep_alive(bool);
  protected:
    int ret;
    SOCKET sock;
    sockaddr_in service;
  };
  struct tcp_socket_t;
  struct tls_socket_t;
  struct tcp_stream_t;
  struct tls_stream_t;
  struct request_handler;




  template <typename socket_t>
  struct server
  {
  public:
    template <typename ...Args>
    server(Args&&... args) : _socket_t(make_shared<socket_t>(std::forward<Args>(args)...)) {}
    server(const server&) = delete;
    server(server&&) = delete;
    auto create();
    auto start();
  private:
    bool load_config();
    bool load_cache();
    shared_ptr<socket_t> _socket_t;
    vector<request_handler> handlers;
    config_t* cfg;
     
  };

  task start();
  void run();
}

#endif //SCHMAL_H
