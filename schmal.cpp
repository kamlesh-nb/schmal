#include "schmal.h"
#include <assert.h>
#include <atomic>
#include <experimental\filesystem>
#include <fstream>
#include <iostream>
#include <regex>

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

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
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
const char ALIGNED(16) header_value[] = "\0\010"   /* allow HT */
                     "\012\037" /* allow SP and up to but not including DEL */
                     "\177\177";
                     
using namespace std;
using namespace rapidjson;
namespace fs = std::experimental::filesystem;

namespace schmal
{
  using Http = asio::ip::tcp::socket;
  using Https = asio::ssl::stream<asio::ip::tcp::socket>;


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
struct parser
{
  void parse(char* buf, size_t length, request &req)
  {
    size_t pret;

    buff = buf;
    size_t len = length;

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
  }
  void parse(char* buf, size_t length, response &res) {}

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

struct request_handler
{
  request_handler() {}
 // void post(stream_t *stream) { _stream = stream; }

private:
   
};

void response::add_header(string &key, string &value)
{
  headers.emplace(key, value);
}


template<class socket_type>
struct session_t : public std::enable_shared_from_this<session_t<socket_type>> {
  template<class ...Args>
  session_t(Args&&... args) : socket(new socket_type(std::forward<Args>(args)...)) {}
  auto read() {
    auto self(shared_from_this());
    promise_t<char*> awaiter;
    auto state = awaiter._state->lock();
    asio::async_read_until(socket, m_streambuf, "\0", [this, self]
    (std::error_code ec, size_t length) {
      if (ec) {
        self->on_error(ec);
      }
    });
    char* output = (char*)malloc(m_streambuf.size());
    memcpy(output, asio::buffer_cast<const void*>(m_streambuf.data()), m_streambuf.size());
    awaiter._state->set_value(output);
    state->unlock();
    return awaiter.get_future();
  }
  auto& write(awaitable_state<size_t>& awaitable, char* data, size_t length) {
    auto self(shared_from_this());
    asio::async_write(socket, asio::buffer(data, length),
      [this, self](std::error_code ec, std::size_t length)
    {
      if (!ec)
      {
        self->on_success(length);
        std::error_code ignored_ec;
        m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
      }
      else {
        self->on_error(ec);
      }
    });
    awaitable.set_value(length);
    return awaitable;
  }
  void on_error(std::error_code ec) {
    err_cd = ec;
  }
  void on_success(size_t len) {
    length = len;
  }
  unique_ptr<socket_type> socket;
private:
  std::error_code err_cd;
  size_t length;
  asio::streambuf m_streambuf;
};


struct acceptor_t{
  acceptor_t(asio::io_context& io_context, 
    string& address, 
    string& port) :
    m_io_context(io_context),
    m_acceptor(io_context),
    m_signals(io_context)
  {
    m_signals.add(SIGINT);
    m_signals.add(SIGTERM);
#if defined(SIGQUIT)
  m_signals.add(SIGQUIT);
#endif // defined(SIGQUIT)
    m_signals.async_wait(std::bind(&acceptor_t::stop, this));
  
    asio::ip::tcp::resolver resolver(io_context);
    asio::ip::tcp::endpoint endpoint =
      *resolver.resolve(address, port).begin();
      m_acceptor.open(endpoint.protocol());
      m_acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
      m_acceptor.bind(endpoint);
      m_acceptor.listen();
  }
  void run(){
    m_io_context.run();
  }
  void stop(){m_acceptor.close();}
private:
  future_t<session_t<Http>> accept(){
    promise_t<session_t<Http>> awaitable;
    
  }
  
  asio::io_context& m_io_context;
  asio::ip::tcp::acceptor m_acceptor;
  asio::signal_set m_signals;
};

struct tls_acceptor_t{
  tls_acceptor_t(asio::io_context& io_context):
  m_io_context(io_context),
  m_acceptor(io_context),
  m_ssl_context(asio::ssl::context::tlsv12)
  {}
private:
  asio::io_context& m_io_context;
  asio::ip::tcp::acceptor m_acceptor;
  asio::ssl::context m_ssl_context;
};

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
    cerr << "app config validation failed for section" << sb.GetString()
         << endl;
    sb.Clear();
    validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
    cerr << "invalid property found in configuration" << sb.GetString() << endl;
    cerr << "terminating....";
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
   
}


// task start()
// {
//  /* server<tcp_socket_t> tcp;
//   tcp.create();
//   while (true)
//   {
//     auto c = co_await tcp.start();
//   }*/
// }

// void run()
// {
//   start();
// }





} //schmal

int main()
{
  //schmal::run();

  asio::io_context ioc;
  schmal::session_t<schmal::Http> sess(ioc);
  schmal::server<schmal::Http> s;
  s.create();
  cout << "this is executed" << endl;

  return 0;
}
