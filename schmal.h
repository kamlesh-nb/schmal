#ifndef SCHMAL_H
#define SCHMAL_H

#include <map>
#include <string>

using namespace std;

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
  namespace awaitable {
    struct acceptor_t;
    struct reader_t;
    struct writer_t;
    struct parser_t;
    struct process_t;
  }
  struct web_context_t
  {
    web_context_t() {}
    void create();
    bool load_config();
    bool load_cache();
    config_t* cfg;
    file_cache* fc;
  };
  namespace http {

  }
  namespace https {

  }
}

#endif //SCHMAL_H
