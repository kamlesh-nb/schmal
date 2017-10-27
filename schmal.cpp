#define ASIO_STANDALONE

#include "schmal.h"
#include <experimental\filesystem>
#include <experimental\coroutine>
#include <fstream>
#include <iostream>

#include <rapidjson/document.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>

#include <zlib.h>
#include <openssl/md5.h>



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
using namespace std::experimental;
namespace fs = std::experimental::filesystem;

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
  namespace util {

    enum class HttpHeader {
      Content_Type = 1,
      Content_Length,
      Last_Modified,
      ETag,
      Content_Encoding,
      Set_Cookie,
      Authorization,
      Access_Control_Allow_Origin,
      Access_Control_Allow_Methods,
      Access_Control_Allow_Headers,
      Access_Control_Max_Age,
      Access_Control_Allow_Credentials,
      Access_Control_Expose_Headers,
      Vary,
      Keep_Alive,
      Connection
    };
    enum class HttpStatus {
      Continue = 100,
      SwitchingProtocols = 101,
      Checkpoint = 103,
      OK = 200,
      Created = 201,
      Accepted = 202,
      NonAuthoritativeInformation = 203,
      NoContent = 204,
      ResetContent = 205,
      PartialContent = 206,
      MultipleChoices = 300,
      MovedPermanently = 301,
      Found = 302,
      SeeOther = 303,
      NotModified = 304,
      SwitchProxy = 306,
      TemporaryRedirect = 307,
      ResumeIncomplete = 308,
      BadRequest = 400,
      Unauthorized = 401,
      PaymentRequired = 402,
      Forbidden = 403,
      NotFound = 404,
      MethodNotAllowed = 405,
      NotAcceptable = 406,
      ProxyAuthenticationRequired = 407,
      RequestTimeout = 408,
      Conflict = 409,
      Gone = 410,
      LengthRequired = 411,
      PreconditionFailed = 412,
      RequestEntityTooLarge = 413,
      RequestURITooLong = 414,
      UnsupportedMediaType = 415,
      RequestedRangeNotSatisfiable = 416,
      ExpectationFailed = 417,
      InternalTcpServerError = 500,
      NotImplemented = 501,
      BadGateway = 502,
      ServiceUnavailable = 503,
      GatewayTimeout = 504,
      HTTPVersionNotSupported = 505,
      NetworkAuthenticationRequired = 511

    };
    struct severity {
      int severity_id;
      const char *severity_type;
    } severities[] =
    {
      { 1, "[LOW]" },
      { 2, "[MEDIUM]" },
      { 3, "[HIGH]" },
      { 4, "[CRITICAL]" },
      { 0, 0 }
    };

    struct header {
      int header_id;
      const char *header_field;
    } headers[] =
    {
      { 1, "Content-Type: " },
      { 2, "Content-Length: " },
      { 3, "Last-Modified: " },
      { 4, "ETag: " },
      { 5, "Content-Encoding: deflate\r\n" },
      { 6, "Set-Cookie: " },
      { 7, "Authorization:" },
      { 8, "Access-Control-Allow-Origin:" },
      { 9, "Access-Control-Allow-Methods:" },
      { 10, "Access-Control-Allow-Headers:" },
      { 11, "Access-Control-Max-Age:" },
      { 12, "Access-Control-Allow-Credentials:" },
      { 13, "Access-Control-Expose-Headers:" },
      { 14, "Vary" },
      { 15, "Keep-Alive" },
      { 16, "Connection" },
      { 0, 0 }
    };

    struct statusphrase {
      int status_code;
      const char *status_phrase;
    } statusphrases[] =
    {
      { 100, "HTTP/1.1 100 Continue" },
      { 101, "HTTP/1.1 101 Switching Protocols" },
      { 103, "HTTP/1.1 103 Checkpoint" },
      { 200, "HTTP/1.1 200 OK" },
      { 201, "HTTP/1.1 201 Created" },
      { 202, "HTTP/1.1 202 Accepted" },
      { 203, "HTTP/1.1 203 Non-Authoritative-Information" },
      { 204, "HTTP/1.1 204 No Content" },
      { 205, "HTTP/1.1 205 Reset Content" },
      { 206, "HTTP/1.1 206 Partial Content" },
      { 300, "HTTP/1.1 300 Multiple Choices" },
      { 301, "HTTP/1.1 301 Moved Permanently" },
      { 302, "HTTP/1.1 302 Found" },
      { 303, "HTTP/1.1 303 See Other" },
      { 304, "HTTP/1.1 304 Not Modified" },
      { 306, "HTTP/1.1 306 Switch Proxy" },
      { 307, "HTTP/1.1 307 Temporary Redirect" },
      { 308, "HTTP/1.1 308 Resume Incomplete" },
      { 400, "HTTP/1.1 400 Bad Request" },
      { 401, "HTTP/1.1 401 Unauthorized" },
      { 402, "HTTP/1.1 402 Payment Required" },
      { 403, "HTTP/1.1 403 Forbidden" },
      { 404, "HTTP/1.1 404 Not Found" },
      { 405, "HTTP/1.1 405 Method Not Allowed" },
      { 406, "HTTP/1.1 406 Not Acceptable" },
      { 407, "HTTP/1.1 407 Proxy Authentication Required" },
      { 408, "HTTP/1.1 408 Request Timeout" },
      { 409, "HTTP/1.1 409 Conflict" },
      { 410, "HTTP/1.1 410 Gone" },
      { 411, "HTTP/1.1 411 Length Required" },
      { 412, "HTTP/1.1 412 Precondition Failed" },
      { 413, "HTTP/1.1 413 Request Entity Too Large" },
      { 414, "HTTP/1.1 414 Request URI Too Large" },
      { 415, "HTTP/1.1 415 Unsupported Media Type" },
      { 416, "HTTP/1.1 416 Requested Range Not Satisfiable" },
      { 417, "HTTP/1.1 417 Expectation Failled" },
      { 500, "HTTP/1.1 500 Internal Server Error" },
      { 501, "HTTP/1.1 501 Not Implemented" },
      { 502, "HTTP/1.1 502 Bad Gateway" },
      { 503, "HTTP/1.1 503 Service Unavailable" },
      { 504, "HTTP/1.1 504 Gateway Timeout" },
      { 505, "HTTP/1.1 505 HTTP Version Not Supported" },
      { 511, "HTTP/1.1 511 Network Authentication Required" },
      { 0,   0 }
    };

    struct statusmsg {
      int status_code;
      const char *status_msg;
    } statusmsgs[] =
    {
      { 100, "Continue" },
      { 101, "Switching Protocols" },
      { 103, "Checkpoint" },
      { 200, "OK" },
      { 201, "Created" },
      { 202, "Accepted" },
      { 203, "Non-Authoritative-Information" },
      { 204, "No Content" },
      { 205, "Reset Content" },
      { 206, "Partial Content" },
      { 300, "Multiple Choices" },
      { 301, "Moved Permanently" },
      { 302, "Found" },
      { 303, "See Other" },
      { 304, "Not Modified" },
      { 306, "Switch Proxy" },
      { 307, "Temporary Redirect" },
      { 308, "Resume Incomplete" },
      { 400, "Bad Request" },
      { 401, "Unauthorized" },
      { 402, "Payment Required" },
      { 403, "Forbidden" },
      { 404, "Not Found" },
      { 405, "Method Not Allowed" },
      { 406, "Not Acceptable" },
      { 407, "Proxy Authentication Required" },
      { 408, "Request Timeout" },
      { 409, "Conflict" },
      { 410, "Gone" },
      { 411, "Length Required" },
      { 412, "Precondition Failed" },
      { 413, "Request Entity Too Large" },
      { 414, "Request URI Too Large" },
      { 415, "Unsupported Media Type" },
      { 416, "Requested Range Not Satisfiable" },
      { 417, "Expectation Failled" },
      { 500, "Internal Server Error" },
      { 501, "Not Implemented" },
      { 502, "Bad Gateway" },
      { 503, "Service Unavailable" },
      { 504, "Gateway Timeout" },
      { 505, "HTTP Version Not Supported" },
      { 511, "Network Authentication Required" },
      { 0,   0 }
    };

    struct mapping {
      const char *extension;
      const char *mime_type;
    } mappings[] =
    {
      { "html",  "text/html; charset=UTF-8" },
      { "htm",   "text/html; charset=UTF-8" },
      { "htmls", "text/html; charset=UTF-8" },
      { "jpe",   "image/jpeg" },
      { "jpeg",  "image/jpeg" },
      { "jpg",   "image/jpeg" },
      { "js",    "application/javascript; charset=UTF-8" },
      { "jsonp", "application/javascript; charset=UTF-8" },
      { "json",  "application/json; charset=UTF-8" },
      { "map",   "application/json; charset=UTF-8" },
      { "gif",   "image/gif" },
      { "css",   "text/css; charset=UTF-8" },
      { "gz",    "application/x-gzip" },
      { "gzip",  "multipart/x-gzip" },
      { "ico",   "image/x-icon" },
      { "png",   "image/png" },
      { 0,       0 }
    };


    const char* cached_date_response = "\r\nDate: ";
    const char* err_cached_response = "\r\nConnection: keep-alive\r\nServer: pigeon\r\nAccept_Range: bytes\r\nContent-Type: text/html; charset=UTF-8\r\n";
    const char* api_cached_response = "\r\nConnection: keep-alive\r\nServer: pigeon\r\nAccept_Range: bytes\r\nContent-Type: application/json\r\n";

    const char *err_msg1 = "<!DOCTYPE html><html><head lang='en'><meta charset='UTF-8'><title>Status</title></head><body><p/><p/><p/><p/><p/><p/><p/><table align=\"center\" style=\"font-family: monospace;font-size: large;background-color: lemonchiffon;border-left-color: green;border-color: red;\"><tr style=\"background: burlywood;\"><th>Status Code</th><th>Message</th></tr><tr><td>";
    const char *err_msg3 = "</td><td>";
    const char *err_msg5 = "</td></tr><tr><td>Description: </td><td>";
    const char *err_msg7 = "</td></tr></table></body></html>";

    const char* api_err_msg1 = "{ \"Status\":";
    const char* api_err_msg2 = ", \"StatusDescription:\"";
    const char* api_err_msg3 = ", \"ErrorDescription\":";
    const char* api_err_msg4 = "}";

    char *now() {
      time_t now = time(0);
      char *dt;
      tm *gmtm = gmtime(&now);
      dt = asctime(gmtm);
      dt[strlen(dt) - 1] = '\0';
      return dt;
    }
    auto get_cached_response(bool is_api, io_buff_t buff){

      buff.save((char *)cached_date_response, strlen((char *)cached_date_response));
      char* ts = now();
      buff.save(ts, strlen(ts));

      if (is_api) {
        buff.save((char *)api_cached_response, strlen((char *)api_cached_response));
      }
    }
    auto get_header_field(HttpHeader hdr){

      for (header *m = headers; m->header_id; ++m) {
        if (m->header_id == static_cast<int>(hdr)) {
          return m->header_field;
        }
      }

      return  "unknown header";
    }
    auto get_header_field(HttpHeader hdr, string &data){
      for (header *m = headers; m->header_id; ++m) {
        if (m->header_id == static_cast<int>(hdr)) {
          data += m->header_field;
        }
      }
    }
    auto get_status_phrase(HttpStatus status, io_buff_t& buff){

      for (statusphrase *m = statusphrases; m->status_code; ++m) {
        if (m->status_code == static_cast<int>(status)) {
          buff.save((char *)m->status_phrase, strlen((char *)m->status_phrase));
        }
      }
    }
    auto get_status_message(HttpStatus status){

      for (statusmsg *m = statusmsgs; m->status_code; ++m) {
        if (m->status_code == static_cast<int>(status)) {
          return m->status_msg;
        }
      }
      return "Unknown";
    }
    auto get_mime_type(string &extension) {
      for (mapping *m = mappings; m->extension; ++m) {
        if (m->extension == extension) {
          return m->mime_type;
        }
        return "text/plain";
      }
      return "Unknown";
    }
    auto deflate_string (string &in, string &out){
    z_stream zs;                        
    memset(&zs, 0, sizeof(zs));
    int compressionlevel = Z_BEST_COMPRESSION;
    if (deflateInit(&zs, compressionlevel) != Z_OK)
      throw (std::runtime_error("deflateInit failed while compressing."));

    zs.next_in = (Bytef *)in.data();
    zs.avail_in = static_cast<unsigned int>(in.size()); 

    int ret;
    char outbuffer[32768];
    std::string outstring;
    do {
      zs.next_out = reinterpret_cast<Bytef *>(outbuffer);
      zs.avail_out = sizeof(outbuffer);

      ret = deflate(&zs, Z_FINISH);

      if (outstring.size() < zs.total_out) {
        outstring.append(outbuffer, zs.total_out - outstring.size());
      }
    } while (ret == Z_OK);
    deflateEnd(&zs);
    if (ret != Z_STREAM_END) {          
      std::ostringstream oss;
      oss << "Exception during zlib compression: (" << ret << ") " << zs.msg;
      throw (std::runtime_error(oss.str()));
    }
    zs.avail_out = zs.total_out;
    out = outstring;
    return outstring.size();
  }
  
  }
  struct session_cache {

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
    int64_t max_request_size;
    _locations locations;
    _net net;
    _store store;
  };
  struct file_t
  {
    string _headers;
    string _def_headers;
    string name;
    string etag;
    string last_write_time;
    const char *data;
    const char *def_data;
    size_t length;
    size_t def_length;
  };
  struct file_cache
  {
    file_cache(string &_path) : path(_path) {}
    void load() {
      for (auto &p : fs::recursive_directory_iterator(path))
      {
        files.emplace(p.path().string(), new file_t);
        cache_item(p.path().string());
      }
    }
    void unload() {
      for (auto& file : files) {
        delete file.second;
      }
      files.clear();
    }
    void get(string& _path, file_t* _file) {
      _file = files[_path];
    }
  private:
    void cache_item(string& _file) {
      string deflated;
      std::ifstream is;
      is.open(_file.c_str(), std::ios::in | std::ios::binary);
      if (is) {
        std::string content((std::istreambuf_iterator<char>(is)), 
          std::istreambuf_iterator<char>());
        
        files[_file]->data = content.c_str();
        files[_file]->length = content.size();
        files[_file]->name = fs::path(_file).filename().string();
        util::deflate_string(content, deflated);
        files[_file]->def_data = deflated.c_str();
        files[_file]->def_length = deflated.size();
        files[_file]->etag = "LLL";
        //get last write time for the file
        auto ftime = fs::last_write_time(fs::path(_file));
        std::time_t cftime = decltype(ftime)::clock::to_time_t(ftime);  
        char* lwt = ctime(&cftime);
        lwt[strlen(lwt) - 1] = '\0';
        files[_file]->last_write_time = lwt;
        //get last write time for the file
       
        md5(lwt, files[_file]);
        string ext = fs::path(_file).extension().string();

        cache_headers(ext, files[_file]);
        cache_def_headers(ext, files[_file]);

      }
    }
    void md5(char* _lwt, file_t* _file) {
      unsigned char digest[16];
      MD5_CTX ctx;
      MD5_Init(&ctx);
      MD5_Update(&ctx, _lwt, strlen(_lwt));
      MD5_Final(digest, &ctx);
      char md5_string[33];
      for (int i = 0; i < 16; i++)
        sprintf(&md5_string[i * 2], "%02x", (unsigned int)digest[i]);

      _file->etag = string(md5_string);
    }
    void cache_headers(string& extension, file_t* _file) {
      _file->_headers += "\r\nCache-Control: public, max-age=0\r\nConnection: keep-alive\r\nServer: schmal\r\nAccept_Range: bytes\r\n";
      util::get_header_field(util::HttpHeader::Content_Type, _file->_headers);
      _file->_headers += util::get_mime_type(extension);
      _file->_headers += "\r\n";

      util::get_header_field(util::HttpHeader::Content_Length, _file->_headers);
      _file->_headers += std::to_string(_file->length);
      _file->_headers += "\r\n";

      util::get_header_field(util::HttpHeader::Last_Modified, _file->_headers);
      _file->_headers += _file->last_write_time;
      _file->_headers += "\r\n";

      util::get_header_field(util::HttpHeader::ETag, _file->_headers);
      _file->_headers += _file->etag;
      _file->_headers += "\r\n";

    }
    void cache_def_headers(string& extension, file_t* _file) {
      _file->_def_headers += "\r\nCache-Control: public, max-age=0\r\nConnection: keep-alive\r\nServer: schmal\r\nAccept_Range: bytes\r\n";
      util::get_header_field(util::HttpHeader::Content_Encoding, _file->_def_headers);
      
      util::get_header_field(util::HttpHeader::Content_Type, _file->_def_headers);
     
      _file->_def_headers += util::get_mime_type(extension);
      _file->_def_headers += "\r\n";

      util::get_header_field(util::HttpHeader::Content_Length, _file->_def_headers);
      _file->_def_headers += std::to_string(_file->def_length);
      _file->_def_headers += "\r\n";

      util::get_header_field(util::HttpHeader::Last_Modified, _file->_def_headers);
      _file->_def_headers += _file->last_write_time;
      _file->_def_headers += "\r\n";

      util::get_header_field(util::HttpHeader::ETag, _file->_def_headers);
      _file->_def_headers += _file->etag;
      _file->_def_headers += "\r\n";

    }
    string path;
    map<string, file_t*> files;
  };
  struct parser
  {
    void parse(http_context_t& context)
    {
      size_t pret;
      buff = context.buffer.get();
      len = context.buffer.length();

      // request line
      {
        pret = find_chars(buff, first_line, len);
        context.Request.method.append(buff, pret);

        move_buff(++pret);
        pret = find_chars(buff, first_line, len);
        context.Request.url.append(buff, pret + 1);

        move_buff(++pret);
        pret = find_chars(buff, first_line, len);
        context.Request.scheme.append(buff, pret);

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
            move_buff(2); --pret; --pret;
            value.append(buff, pret);
            move_buff(pret);
            context.Request.headers.emplace(name, value);
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
        context.Request.body.append(buff);
      }
      // request body
    }
    void parse(char* buf, size_t length, response &res) {}
  private:
    char *buff;
    size_t len;
    size_t findchar_fast(const char *str, size_t _len, const char *ranges, int ranges_sz, int *found)
    {
      __m128i ranges16 = _mm_loadu_si128((const __m128i *)ranges);
      const char *s = str;
      size_t left = _len & ~0xf;
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
    size_t find_chars(const char *str, const char *ranges, size_t _len)
    {
      const char *s;
      size_t n = 0;
      if (_len >= 16)
      {
        int found;
        n = findchar_fast(str, _len, ranges, sizeof(first_line) - 1, &found);
        if (found)
          return n;
      }
      s = str + n;
      while (s - str < _len && uri_a[*s])
        ++s;
      return s - str;
    }
    void move_buff(size_t _len)
    {
      for (size_t i = 0; i < _len; ++i)
      {
        ++buff;
      }
    }
  };
  string request::get_header(string& key) {
    return headers[key];
  }
  string request::get_cookie(string& key) {
    return cookies[key];
  }
  void response::add_header(string &key, string &value) {
    headers.emplace(key, value);
  }
  void response::add_cookie(string& key, string& value) {
    cookies.emplace(key, value);
  }
  void response::create() {

  }
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
      reader_t(shared_ptr<http_context_t> p_http_context_t) : m_http_context_t(std::move(p_http_context_t)) {
        data = new char[m_http_context_t->AppContext->config->max_request_size]();
      }
      bool await_ready() noexcept { return _ready; }
      auto await_resume() {
        if (e)
          throw std::system_error(e);
        delete data;
        return m_http_context_t;
      }
      auto await_suspend(coroutine_handle<> coro) {
        //this may cause problems, tried async_read_until, but it didn't read all data, added some junk characters at the end
          m_http_context_t->Socket.async_read_some(asio::buffer(data, m_http_context_t->AppContext->config->max_request_size),
            [this, coro](std::error_code ec, std::size_t length)
          {
            if (!ec) { m_http_context_t->buffer.save(data, length); }
            e = ec;
            len = length;
            _ready = true;
            coro.resume();
          });
        return true;
      }
      shared_ptr<http_context_t> m_http_context_t;
      char* data;
      std::error_code e;
      size_t len;
    };
    struct parser_t {
      bool _ready = false;
      parser_t(shared_ptr<http_context_t> p_http_context_t) :
        m_http_context_t(std::move(p_http_context_t)) {}
      bool await_ready() noexcept {
        return _ready;
      }
      void await_suspend(coroutine_handle<> coro) noexcept {
        _parser.parse(*m_http_context_t);
        _ready = true;
        coro.resume();

      }
      auto await_resume() {
        if (e)
          throw std::system_error(e);
        return m_http_context_t;
      }
      shared_ptr<http_context_t> m_http_context_t;
      std::error_code e;
      parser _parser;
    };
    struct process_t {
      bool _ready = false;
      process_t(shared_ptr<http_context_t> p_http_context_t) :
        m_http_context_t(std::move(p_http_context_t)) {}
      bool await_ready() noexcept {
        return _ready;
      }
      void await_suspend(coroutine_handle<> coro) noexcept {

        _ready = true;
        coro.resume();

      }
      auto await_resume() {
        if (e)
          throw std::system_error(e);
        return m_http_context_t;
      }
      shared_ptr<http_context_t> m_http_context_t;
      std::error_code e;
    };
    struct writer_t {
      bool _ready = false;
      writer_t(shared_ptr<http_context_t> p_http_context_t) : m_http_context_t(std::move(p_http_context_t)) {}
      bool await_ready() noexcept { return _ready; }
      auto await_resume() {
        if (e)
          throw std::system_error(e);
        m_http_context_t->Socket.shutdown(asio::ip::tcp::socket::shutdown_both, e);
        m_http_context_t->Response.buffer.clear();
        return true;
      }
      auto await_suspend(coroutine_handle<> coro) {
        asio::async_write(m_http_context_t->Socket,
          asio::buffer(m_http_context_t->Response.buffer.get(), m_http_context_t->Response.buffer.length()),
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
  } //awaitable
  bool app_context_t::load_config()
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
    config = new config_t;

    // read app section
    config->name = configDoc["app"]["name"].GetString();
    config->defaultPage = configDoc["app"]["defaultPage"].GetString();
    config->apiRoute = configDoc["app"]["apiRoute"].GetString();
    config->workers = configDoc["app"]["workers"].GetInt();

    config->max_request_size = configDoc["app"]["max_request_size"].GetInt64();

    config->locations.docLocation =
      configDoc["app"]["locations"]["docLocation"].GetString();
    config->locations.logLocation =
      configDoc["app"]["locations"]["logLocation"].GetString();
    if (configDoc["app"]["locations"].HasMember("uploadLocation"))
    {
      config->locations.uploadLocation =
        configDoc["app"]["locations"]["uploadLocation"].GetString();
    }
    config->net.ip = configDoc["net"]["ip"].GetString();
    config->net.port = configDoc["net"]["port"].GetString();
    config->net.nodelay = configDoc["net"]["nodelay"].GetBool();
    config->net.nagle = configDoc["net"]["nagle"].GetBool();

    config->net.tls.version = configDoc["net"]["tls"]["version"].GetString();
    config->net.tls.cert = configDoc["net"]["tls"]["cert"].GetString();
    config->net.tls.key = configDoc["net"]["tls"]["key"].GetString();

    config->store.host = configDoc["store"]["host"].GetString();
    config->store.port = configDoc["store"]["port"].GetString();
    config->store.userid = configDoc["store"]["userid"].GetString();
    config->store.passwd = configDoc["store"]["passwd"].GetString();

    config->store.provider.name = configDoc["store"]["provider"]["name"].GetString();
    config->store.provider.type = configDoc["store"]["provider"]["type"].GetString();

    return true;
  }
  void app_context_t::load_cache()
  {
    filecache = new file_cache(config->locations.docLocation);
    filecache->load();
  }
  api_route_handler app_context_t::get(string & handler_name)
  {
    return handlers[handler_name];
  }
  void app_context_t::add_route_handler(string handler_name, api_route_handler handler)
  {
    handlers.emplace(handler_name, handler);
  }
  void app_context_t::create()
  {
    load_config();
    load_cache();
  }
  auto accept(tcp::acceptor& a) {
    return awaitable::acceptor_t{ a };
  }
  auto read(shared_ptr<http_context_t> p_http_context_t) {
    awaitable::reader_t r(p_http_context_t);
    return awaitable::reader_t{ p_http_context_t };
  }
  auto parse(shared_ptr<http_context_t> p_http_context_t) {
    return awaitable::parser_t{ p_http_context_t };
  }
  auto process(shared_ptr<http_context_t> p_http_context_t) {
    return awaitable::parser_t{ p_http_context_t };
  }
  auto write(shared_ptr<http_context_t> p_http_context_t) {
    return awaitable::writer_t{ p_http_context_t };
  }
  namespace http {
    task respond(shared_ptr<http_context_t> p_http_context_t) {
      try
      {
        auto ret = co_await schmal::write(p_http_context_t);
      }
      catch (std::exception& e)
      {
        std::cout << "error: " << e.what() << std::endl;
      }
    }
    task process_request(shared_ptr<http_context_t> p_http_context_t) {
      try
      {
        auto ret = co_await schmal::process(p_http_context_t);
        schmal::http::respond(p_http_context_t);
      }
      catch (std::exception& e)
      {
        std::cout << "error: " << e.what() << std::endl;
      }
    }
    task parse_request(shared_ptr<http_context_t> p_http_context_t) {
      try
      {
        auto ret = co_await schmal::parse(p_http_context_t);
        schmal::http::process_request(p_http_context_t);
      }
      catch (std::exception& e)
      {
        std::cout << "error: " << e.what() << std::endl;
      }
    }
    task read_request(shared_ptr<http_context_t> p_http_context_t)
    {
      try
      {
        auto ret = co_await schmal::read(p_http_context_t);
        schmal::http::parse_request(p_http_context_t);
      }
      catch (std::exception& e)
      {
        std::cout << "error: " << e.what() << std::endl;
      }
    }
    task accept(asio::io_context& io, app_context_t* wct)
    {
      asio::ip::tcp::resolver resolver{ io };
      asio::ip::tcp::endpoint endpoint = *resolver.resolve(wct->config->net.ip, wct->config->net.port).begin();
      asio::ip::tcp::acceptor acceptor{ io, endpoint };
      acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
      asio::ip::tcp::socket sock{ io };
      for (; ;)
      {
        auto _sock = co_await schmal::accept(acceptor);
        auto _context = make_shared<http_context_t>(std::move(_sock), wct);
        schmal::http::read_request(_context);
      }
    }
  }
} //schmal

int main()
{
  asio::io_context io;
  schmal::app_context_t* wct = new schmal::app_context_t;
  wct->create();
  wct->add_route_handler("/hello", [](schmal::http_context_t* context) {
     
  });
  schmal::http::accept(io, wct);
  io.run();

  return EXIT_SUCCESS;
}
