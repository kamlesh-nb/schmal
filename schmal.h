#ifndef SCHMAL_H
#define SCHMAL_H

#include <map>
#include <string>

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
    bool api;
  };
  struct response
  {
    int status;
    map<string, string> headers;
    map<string, string> cookies;
    string body;
    io_buff_t buffer;
    void add_header(string&, string&);
    void add_cookies(string&, string&);
    void create();
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
