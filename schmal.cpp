#include "schmal.h"
#include <assert.h>
#include <atomic>
#include <experimental\filesystem>
#include <fstream>
#include <iostream>

#include <rapidjson/document.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>

#ifdef _MSC_VER
#include <nmmintrin.h>
#define likely(x) (x)
#define unlikely(x) (x)
#define ALIGNED(n) _declspec(align(n))
#else
#include <x86intrin.h>
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define ALIGNED(n) __attribute__((aligned(n)))
#endif

#define PATH_BUFFER_SIZE 2048 // current working directory

const char ALIGNED(64) uri_a[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
const char ALIGNED(16) first_line[] = "\x00 "     /* control chars and up to SP */
                                      "\"\""      /* 0x22 */
                                      "<<"        /* 0x3c,0x3c */
                                      ">>"        /* 0x3e,0x3e */
                                      "\\\\"      /* 0x5c,0x5c */
                                      "^^"        /* 0x5e,0x5e */
                                      "{}"        /* 0x7b-0x7d */
                                      "\x7f\xff"; /* 0x7f-0xff */
const char ALIGNED(16) header_name[] = "\x00 "    /* control chars and up to SP */
                                       "\"\""     /* 0x22 */
                                       "()"       /* 0x28,0x29 */
                                       ",,"       /* 0x2c */
                                       "//"       /* 0x2f */
                                       ":@"       /* 0x3a-0x40 */
                                       "[]"       /* 0x5b-0x5d */
                                       "{\377";   /* 0x7b-0xff */
const char ALIGNED(16)
    header_value[] = "\0\010"   /* allow HT */
                     "\012\037" /* allow SP and up to but not including DEL */
                     "\177\177";

using namespace std;
using namespace rapidjson;
namespace fs = std::experimental::filesystem;

namespace schmal
{

void ERR(const char *msg, int err)
{
#ifdef _WIN32
  std::cerr << msg << ": " << WSAGetLastError() << std::endl;
  WSACleanup();
  assert(err);
#else
  std::cerr << msg << ": " << errno() << std::endl;
  assert(err);
#endif
}
struct config_t
{
  struct _net
  {
    struct _tls
    {
      string version;
      string cert;
      string key;
    };
    string ip;
    short port;
    bool nodelay;
    bool nagle;
    _tls tls;
  };
  struct _store
  {
    struct _provider
    {
      string name;
      string type;
    };
    string host;
    int port;
    string userid;
    string passwd;
    _provider provider;
  };
  struct _locations
  {
    string docLocation;
    string logLocation;
    string uploadLocation;
  };
  string name;
  string defaultPage;
  string apiRoute;
  int workers;
  _locations locations;
  _net net;
  _store store;
};
struct file_t
{
  char *_headers;
  char *_def_headers;
  char *name;
  char *etag;
  char *last_write_time;
  char *data;
  char *def_data;
  size_t length;
  size_t def_length;
};
struct file_cache
{
  file_cache(string &_path) : path(_path) {}
  void load() {}
  void unload() {}
  void get(file_t &file) {}

private:
  string path;
  map<string, file_t> files;
};
struct io_buff_t
{
  io_buff_t(int capacity)
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
struct parser
{
  void parse(io_buff_t &buf, request &req)
  {
    size_t pret;

    buff = buf.get();
    int len = buf.length();

    // request line
    {
      pret = find_first_line(buff, len);
      req.method.append(buff, pret);

      move_buff(++pret);
      pret = find_first_line(buff, len);
      req.url.append(buff, pret + 1);

      move_buff(++pret);
      pret = find_first_line(buff, len);
      req.scheme.append(buff, pret);

      move_buff(pret);
      pret = find_first_line(buff, len);
    }
    // request line end

    string name, value;
    int num_zeroes = 0;
    // request headers
    while (!pret)
    {
      pret = find_header_name(buff, len);
      if (pret)
      {
        if (name.empty())
        {
          name.append(buff, pret);
          move_buff(pret);
          pret = find_header_value(buff, len);
          move_buff(1);
          value.append(buff, --pret);
          move_buff(--pret);
          req.headers.emplace(name, value);
          name.clear();
          value.clear();
        }
        pret = 0;
        num_zeroes = 0;
      }
      else
      {
        ++num_zeroes;
        move_buff(1);
        if (num_zeroes == 4)
        {
          break;
        }
      }
    }
    // request headers

    // request body
    {
      // buffer.Skip(num_zeroes);
      req.body.append(buff);
    }
    // request body

    // buffer.Remove(buffer.Length()-1);
    buf.clear();
  }
  void parse(io_buff_t &buf, response &res) {}

private:
  char *buff;
  size_t findchar_fast(const char *str, size_t len, const char *ranges,
                       int ranges_sz, int *found)
  {
    __m128i ranges16 = _mm_loadu_si128((const __m128i *)ranges);
    const char *s = str;
    size_t left = len & ~0xf;
    *found = 0;
    do
    {
      __m128i b16 = _mm_loadu_si128((const __m128i *)s);
      int r = _mm_cmpestri(ranges16, ranges_sz, b16, 16,
                           _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES |
                               _SIDD_UBYTE_OPS);
      if (r != 16)
      {
        *found = 1;
        return s - str + r;
      }
      s += 16;
      left -= 16;
    } while (left);
    return s - str;
  }
  size_t find_first_line(const char *str, size_t len)
  {
    const char *s;
    size_t n = 0;
    if (len >= 16)
    {
      int found;
      n = findchar_fast(str, len, first_line, sizeof(first_line) - 1, &found);
      if (found)
        return n;
    }
    s = str + n;
    while (s - str < len && uri_a[*s])
      ++s;
    return s - str;
  }
  size_t find_header_name(const char *str, size_t len)
  {
    const char *s;
    size_t n = 0;
    if (len >= 16)
    {
      int found;
      n = findchar_fast(str, len, header_name, sizeof(header_name) - 1, &found);
      if (found)
        return n;
    }
    s = str + n;
    while (s - str < len && uri_a[*s])
      ++s;
    return s - str;
  }
  size_t find_header_value(const char *str, size_t len)
  {
    const char *s;
    size_t n = 0;
    if (len >= 16)
    {
      int found;
      n = findchar_fast(str, len, header_value, sizeof(header_value) - 1,
                        &found);
      if (found)
        return n;
    }
    s = str + n;
    while (s - str < len && uri_a[*s])
      ++s;
    return s - str;
  }
  void move_buff(size_t len)
  {
    for (size_t i = 0; i < len; ++i)
    {
      ++buff;
    }
  }
};
struct tcp_stream_t : public stream_t
{
  tcp_stream_t(SOCKET sock) : sock(sock) {}
  void read(io_buff_t &buff) override
  {
    int bytes;
    do
    {
      char *data = (char *)malloc(sizeof(char) * 512);
      bytes = ::recv(sock, data, 512, 0);
      if (bytes == SOCKET_ERROR)
        ERR("recv() failed with error", bytes);
      buff.save(data, bytes);
      free(data);
      if (bytes < 512)
        break;
    } while (bytes > 0);
  }
  int write(char *data, size_t len) override
  {
    ret = ::send(sock, data, len, 0);
    if (ret == INVALID_SOCKET)
      ERR("send() failed with error", ret);
    return ret;
  }
  int close() override
  {
    ret = ::closesocket(sock);
    if (ret == INVALID_SOCKET)
      ERR("close failed with error", ret);
    return ret;
  }

private:
  SOCKET sock;
  int ret;
};

int socket_t::create()
{
#ifdef _WIN32
  WSADATA wsaData;
  ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (ret != NO_ERROR)
    ERR("WSAStartup failed with error", ret);
#endif
  // create socket, bind and start listening
  sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET)
    ERR("socket() failed with error", sock);
  return 1;
}
int socket_t::bind(string &address, short port)
{
  service.sin_family = AF_INET;
  service.sin_addr.s_addr = inet_addr(address.c_str());
  service.sin_port = htons(port);
  ret = ::bind(sock, (SOCKADDR *)&service, sizeof(service));
  if (ret == SOCKET_ERROR)
    ERR("bind() failed with error", sock);
  return ret;
}
int socket_t::listen()
{
  ret = ::listen(sock, SOMAXCONN);
  if (ret == SOCKET_ERROR)
    ERR("listen() failed with error", ret);
  return ret;
}
int socket_t::set_non_blocking(bool _mode)
{
  u_long mode;
  if (_mode)
  {
    mode = 1;
  }
  else
  {
    mode = 0;
  }
  int ret = ioctlsocket(sock, FIONBIO, &mode);
  if (ret != NO_ERROR)
    ERR("ioctlsocket() failed with error", ret);

  return ret;
}
int socket_t::set_tcp_no_delay(bool set)
{
  BOOL bOptVal = set;
  int bOptLen = sizeof(BOOL);
  int iOptVal = 0;
  ret = setsockopt(sock, SOL_SOCKET, TCP_NODELAY, (char *)&bOptVal, bOptLen);
  if (ret == SOCKET_ERROR)
    ERR("setsockopt() failed with error", ret);

  return ret;
}
int socket_t::set_tcp_keep_alive(bool set)
{
  BOOL bOptVal = set;
  int bOptLen = sizeof(BOOL);
  int iOptVal = 0;
  ret = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&bOptVal, bOptLen);
  if (ret == SOCKET_ERROR)
    ERR("setsockopt() failed with error", ret);

  return ret;
}

struct tcp_socket_t : public socket_t
{
  SOCKET accept()
  {
    SOCKET asock = ::accept(sock, NULL, NULL);
    if (asock == INVALID_SOCKET)
      ERR("accept() failed with error", asock);
    return asock;
  }
  int connect() {}

private:
};
struct tls_stream_t : public stream_t
{
  void read(io_buff_t &buff) override {}
  int write(char *data, size_t len) override { return 0; }
  int close() override { return 0; }
};
struct tls_socket_t : public socket_t
{
  int accept() {}
  int connect() {}

private:
};

struct request_handler
{
  request_handler() {}
  void post(stream_t *stream) { _stream = stream; }

private:
  stream_t *_stream;
  atomic<bool> done;
};

void response::add_header(string &key, string &value)
{
  headers.emplace(key, value);
}

template <typename socket_t>
bool server<socket_t>::load_config()
{
  char _cwd[PATH_BUFFER_SIZE];
  getcwd(_cwd, PATH_BUFFER_SIZE);
  Document schemaDoc;
  Document configDoc;
  string path("D:\\repos\\myWork\\schmal");
  std::ifstream iconfig(path.append("\\app.config").c_str(),
                        std::ios::in | std::ios::binary);
  std::string configJson((std::istreambuf_iterator<char>(iconfig)),
                         (std::istreambuf_iterator<char>()));

  string schema_file("D:\\repos\\myWork\\schmal\\app.schema.json");
  std::ifstream ischema(schema_file.c_str(), std::ios::in | std::ios::binary);
  std::string schemaJson((std::istreambuf_iterator<char>(ischema)),
                         (std::istreambuf_iterator<char>()));

  if (schemaDoc.Parse(schemaJson.c_str()).HasParseError())
  {
    cout << "Schema Parse error..." << endl;
    return false;
  }
  SchemaDocument schema(schemaDoc);

  if (configDoc.Parse(configJson.c_str()).HasParseError())
  {
    cout << "Config Parse error..." << endl;
    return false;
  }

  SchemaValidator validator(schema);
  if (!configDoc.Accept(validator))
  {
    StringBuffer sb;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
    cerr << "build config validation failed for section" << sb.GetString()
         << endl;
    sb.Clear();
    validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
    cerr << "invalid property found in configuration" << sb.GetString() << endl;
    cerr << "terminating build....";
    return false;
  }

  // initialize config
  cfg = new config_t;

  // read app section
  cfg->name = configDoc["app"]["name"].GetString();
  cfg->defaultPage = configDoc["app"]["defaultPage"].GetString();
  cfg->apiRoute = configDoc["app"]["apiRoute"].GetString();
  cfg->workers = configDoc["app"]["workers"].GetInt();

  cfg->workers = configDoc["app"]["workers"].GetInt();
  cfg->locations.docLocation =
      configDoc["app"]["locations"]["docLocation"].GetString();
  cfg->locations.logLocation =
      configDoc["app"]["locations"]["logLocation"].GetString();
  if (configDoc["app"]["locations"].HasMember("uploadLocation"))
  {
    cfg->locations.uploadLocation =
        configDoc["app"]["locations"]["uploadLocation"].GetString();
  }
  cfg->net.ip = configDoc["net"]["ip"].GetString();
  cfg->net.port = configDoc["net"]["port"].GetInt();
  cfg->net.nodelay = configDoc["net"]["nodelay"].GetBool();
  cfg->net.nagle = configDoc["net"]["nagle"].GetBool();

  cfg->net.tls.version = configDoc["net"]["tls"]["version"].GetString();
  cfg->net.tls.cert = configDoc["net"]["tls"]["cert"].GetString();
  cfg->net.tls.key = configDoc["net"]["tls"]["key"].GetString();

  cfg->store.host = configDoc["store"]["host"].GetString();
  cfg->store.port = configDoc["store"]["port"].GetInt();
  cfg->store.userid = configDoc["store"]["userid"].GetString();
  cfg->store.passwd = configDoc["store"]["passwd"].GetString();

  cfg->store.provider.name = configDoc["store"]["provider"]["name"].GetString();
  cfg->store.provider.type = configDoc["store"]["provider"]["type"].GetString();

  return true;
}

template <typename socket_t>
bool server<socket_t>::load_cache()
{
  return true;
}
template <typename socket_t>
inline auto server<socket_t>::create()
{
  load_config();
  load_cache();
  _socket_t->create();
  _socket_t->bind(cfg->net.ip, cfg->net.port);
  _socket_t->set_tcp_no_delay(true);
  _socket_t->set_tcp_keep_alive(true);
  //_socket_t->set_non_blocking(true);
  _socket_t->listen();
}
template <typename socket_t>
inline auto server<socket_t>::start()
{
  promise_t<SOCKET> awaiter;
  auto state = awaiter._state->lock();
  auto ret = _socket_t->accept();
  if (ret == INVALID_SOCKET)
    ERR("accept() failed with error", ret);

  awaiter._state->set_value(ret);
  state->unlock();
  return awaiter.get_future();
}

task start()
{
  server<tcp_socket_t> tcp;
  tcp.create();
  while (true)
  {
    auto c = co_await tcp.start();
  }
}

void run()
{
  start();
}
} //schmal

int main()
{
  schmal::run();
  return 0;
}
