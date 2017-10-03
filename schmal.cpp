#include "schmal.h"
#include <experimental\filesystem>
#include <experimental\coroutine>
#include <fstream>
#include <iostream>

#include <rapidjson/document.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>



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
using asio::ip::tcp;
using namespace std::experimental;

namespace schmal
{
  struct task {
    ~task() {}
    struct promise_type {
      task get_return_object() { return task{}; }
      void return_void() {}
      bool initial_suspend() { return false; }
      bool final_suspend() { return false; }
    };
  };

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
      string port;
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
      string port;
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
    string *name;
    string *etag;
    string *last_write_time;
    const char *data;
    const char *def_data;
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
        pret = find_chars(buff,first_line, len);
        req.method.append(buff, pret);

        move_buff(++pret);
        pret = find_chars(buff, first_line, len);
        req.url.append(buff, pret + 1);

        move_buff(++pret);
        pret = find_chars(buff, first_line, len);
        req.scheme.append(buff, pret);

        move_buff(pret);
        pret = find_chars(buff, first_line, len);
      }
      // request line end

      string name, value;
      int num_zeroes = 0;
      // request headers
      while (!pret)
      {
        pret = find_chars(buff, header_name, len);
        if (pret)
        {
          if (name.empty())
          {
            name.append(buff, pret);
            move_buff(pret);
            pret = find_chars(buff, header_value, len);
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
    size_t findchar_fast(const char *str, size_t len, const char *ranges, int ranges_sz, int *found)
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
    size_t find_chars(const char *str, const char *ranges, size_t len)
    {
      const char *s;
      size_t n = 0;
      if (len >= 16)
      {
        int found;
        n = findchar_fast(str, len, ranges, sizeof(first_line) - 1, &found);
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
  void response::add_header(string &key, string &value){
    headers.emplace(key, value);
  }
  void response::add_cookies(string& key, string& value){
    cookies.emplace(key, value);
  }
  void response::create(){
     
  }

  struct http_context_t{
    http_context_t(tcp::socket sock, 
      config_t* cfg) : _socket(std::move(sock)), 
      _config(cfg){}
    request _request;
    response _response;
    tcp::socket _socket;
    string _data;
    asio::streambuf _streambuf;
    config_t* _config;
  };

  namespace awaitable {
    struct acceptor_t {
      bool _ready = false;
      acceptor_t(tcp::acceptor& p_acceptor) :
        m_acceptor(p_acceptor),
        m_socket(p_acceptor.get_io_context()) {}
      bool await_ready() noexcept {
        return _ready;
      }
      void await_suspend(coroutine_handle<> coro) noexcept {
        m_acceptor.async_accept(m_socket, [this, coro](std::error_code ec) {
          e = ec;
          _ready = true;
          coro.resume();
        });
      }
      auto await_resume() {
        if (e)
          throw std::system_error(e);
        return std::move(m_socket);
      }
      tcp::acceptor& m_acceptor;
      tcp::socket m_socket;
      std::error_code e;
    };
    struct reader_t {
      bool _ready = false;
      reader_t(shared_ptr<http_context_t> p_http_context_t) : m_http_context_t(p_http_context_t) {}
      bool await_ready() noexcept { return _ready; }
      auto await_resume() {
        if (e)
          throw std::system_error(e);
        using asio::buffers_begin;
        auto bufs = m_http_context_t->_streambuf.data();
        m_http_context_t->_data.append(buffers_begin(bufs), buffers_begin(bufs) + m_http_context_t->_streambuf.size());
        return m_http_context_t;
      }
      auto await_suspend(coroutine_handle<> coro) {
        asio::async_read_until(m_http_context_t->_socket, m_http_context_t->_streambuf, '\0', [this, coro]
        (std::error_code ec, size_t length) {
          e = ec;
          len = length;
          _ready = true;
          coro.resume();
        });
        return true;
      }
      shared_ptr<http_context_t> m_http_context_t;
      
      std::error_code e;
      size_t len;
    };
    struct writer_t {
      bool _ready = false;
      writer_t(shared_ptr<http_context_t> p_http_context_t) : m_http_context_t(p_http_context_t) {}
      bool await_ready() noexcept { return _ready; }
      auto await_resume() {
        if (e)
          throw std::system_error(e);
          m_http_context_t->_socket.shutdown(asio::ip::tcp::socket::shutdown_both, e);
          m_http_context_t->_response.buffer.clear();
        return true;
      }
      auto await_suspend(coroutine_handle<> coro) {
        asio::async_write(m_http_context_t->_socket, 
          asio::buffer(m_http_context_t->_response.buffer.get(), m_http_context_t->_response.buffer.length()), 
          [this, coro]
        (std::error_code ec, size_t length) {
          e = ec;
          len = length;
          _ready = true;
          coro.resume();
        });
        return true;
      }
      shared_ptr<http_context_t> m_http_context_t;
      std::error_code e;
      size_t len;
      char* buff;
    };

    struct dummy{
      dummy(tcp::socket sock) : socket(std::move(sock)){}
      bool await_ready(){return false;}
      void await_suspend(coroutine_handle<> coro) { coro.resume();}
      auto await_resume(){return string("test");}
      tcp::socket socket;
    };
  }
  bool web_context_t::load_config()
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
    cfg->net.port = configDoc["net"]["port"].GetString();
    cfg->net.nodelay = configDoc["net"]["nodelay"].GetBool();
    cfg->net.nagle = configDoc["net"]["nagle"].GetBool();

    cfg->net.tls.version = configDoc["net"]["tls"]["version"].GetString();
    cfg->net.tls.cert = configDoc["net"]["tls"]["cert"].GetString();
    cfg->net.tls.key = configDoc["net"]["tls"]["key"].GetString();

    cfg->store.host = configDoc["store"]["host"].GetString();
    cfg->store.port = configDoc["store"]["port"].GetString();
    cfg->store.userid = configDoc["store"]["userid"].GetString();
    cfg->store.passwd = configDoc["store"]["passwd"].GetString();

    cfg->store.provider.name = configDoc["store"]["provider"]["name"].GetString();
    cfg->store.provider.type = configDoc["store"]["provider"]["type"].GetString();

    return true;
  }
  bool web_context_t::load_cache()
  {
    return true;
  }
  void web_context_t::create()
  {
    load_config();
    load_cache();
  }
  auto accept(tcp::acceptor& a) {
    return awaitable::acceptor_t{ a };
  }
  auto read(shared_ptr<http_context_t> p_http_context_t){
    return awaitable::reader_t{p_http_context_t};
  }
  auto write(shared_ptr<http_context_t> p_http_context_t){
    return awaitable::writer_t { p_http_context_t };
  }

  namespace http {
    task read(shared_ptr<http_context_t> p_http_context_t)
    {
      try
      {
         auto ret = co_await schmal::read(p_http_context_t);
         std::cout << ret->_data << std::endl;
      }
      catch (std::exception& e)
      {
        std::cout << "error: " << e.what() << std::endl;
      }
    }
    task accept(asio::io_context& io, web_context_t* wct)
    {
      asio::ip::tcp::resolver resolver{ io };
      asio::ip::tcp::endpoint endpoint = *resolver.resolve(wct->cfg->net.ip, wct->cfg->net.port).begin();
      asio::ip::tcp::acceptor acceptor{ io, endpoint };
      acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
      asio::ip::tcp::socket sock{ io };
      for (; ; )
      {
        auto sock = co_await schmal::accept(acceptor);
        auto _context = make_shared<http_context_t>(std::move(sock), wct->cfg);
        schmal::http::read(_context);
      }
    }
  }

} //schmal

int main()
{
  asio::io_context io;
  schmal::web_context_t* wct = new schmal::web_context_t;
  wct->create();
  schmal::http::accept(io, wct);
  io.run();

  return EXIT_SUCCESS;
}

