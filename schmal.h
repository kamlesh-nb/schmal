#ifndef SCHMAL_H
#define SCHMAL_H

#include <map>
#include <string>

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>

using asio::ip::tcp;
using namespace std;

namespace schmal {
  struct config_t;
  struct io_buff_t
  {
    io_buff_t() {}
    void create(int capacity)
    {
      _base = (char *)malloc(sizeof(char) * capacity);
      _len = capacity;
      _curpos = 0;
      _slab = capacity;
      memset(_base, '\0', _slab);
    }
    void reset()
    {
      if (_base)
        return;
      _base = (char *)malloc(sizeof(char) * _slab);
      _len = _slab;
      _curpos = 0;
      memset(_base, '\0', _slab);
    }
    void save(char *str, int bytes)
    {
      if (_curpos > 0 && _curpos < _slab)
      {
        if (_curpos + bytes > _slab)
        {
          _base = (char *)realloc(_base, bytes - _slab);
          memcpy(_base + _curpos, str, bytes);
        }
        else
        {
          memcpy(_base + _curpos, str, bytes);
        }
      }
      else if (_curpos == 0)
      {
        memcpy(_base + _curpos, str, bytes);
      }
      else if (_curpos >= _slab)
      {
        _base = (char *)realloc(_base, _curpos + bytes);
        memset(_base + _curpos, '\0', bytes);
        memcpy(_base + _curpos - 1, str, bytes);
      }
      _curpos += bytes;
      _len = _curpos;
    }
    char *get() { return _base; }
    int length() { return _len; }
    void clear()
    {
      if (_len == 0) return;
      _curpos = 0;
      _len = 0;
      free(_base);
    }

  private:
    char *_base;
    int _curpos, _slab, _len = 0;
  };
  struct request {
    string method;
    string url;
    string scheme;
    string body;
    map<string, string> headers;
    map<string, string> cookies;
    string get_header(string&);
    string get_cookie(string&);
    bool api;
  private:
    io_buff_t buffer;
  };
  struct response
  {
    int status;
    map<string, string> headers;
    map<string, string> cookies;
    string body;
    
    void add_header(string&, string&);
    void add_cookie(string&, string&);
    void create();
    io_buff_t buffer;
  };
  struct parser;
  struct file_cache;
  struct http_context_t;
  typedef std::function<void(http_context_t*)> api_route_handler;
  struct app_context_t
  {
    app_context_t() {}
    void create();
    bool load_config();
    void load_cache();
    config_t* config;
    file_cache* filecache;
    api_route_handler get(string& handler_name);
    void add_route_handler(string handler_name, api_route_handler handler);
  private:
    map<string, api_route_handler> handlers;
  };
  struct http_context_t {
    http_context_t(tcp::socket sock,
      app_context_t* cfg) : Socket(std::move(sock)),
      AppContext(cfg) {
      buffer.create(1024);
    }
    request Request;
    response Response;
    tcp::socket Socket;
    app_context_t* AppContext;
    io_buff_t buffer;
  };
  namespace awaitable {
    struct acceptor_t;
    struct reader_t;
    struct writer_t;
    struct parser_t;
    struct process_t;
  }
  namespace http {

  }
  namespace https {

  }

}

#endif //SCHMAL_H
