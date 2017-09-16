// templates.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

//#include "Awaitable.h"
//
//
//
//int main()
//{
//  awaitable::run();
//  return 0;
//}


#include "awaitio.h"
#include <iostream>

using namespace std;
using namespace awaitio;

#define  MAXBUFF 665536

struct Tcp {
  struct Socket {
    Socket(){
      int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
      if (iResult != NO_ERROR) {
        wprintf(L"WSAStartup failed with error: %ld\n", iResult);
      }
      sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (sock == INVALID_SOCKET) {
        wprintf(L"socket failed with error: %ld\n", WSAGetLastError());
        WSACleanup();
      }
    }
    void Bind(const char* address, USHORT port) {
      service.sin_family = AF_INET;
      service.sin_addr.s_addr = inet_addr(address);
      service.sin_port = htons(port);
      if (::bind(sock,(SOCKADDR *)& service, sizeof(service)) == SOCKET_ERROR) {
        wprintf(L"bind failed with error: %ld\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return;
      }
    }
    void Connect() {
      int iResult = connect(sock, (SOCKADDR *)& service, sizeof(service));
      if (iResult == SOCKET_ERROR) {
        wprintf(L"connect function failed with error: %ld\n", WSAGetLastError());
        iResult = closesocket(sock);
        if (iResult == SOCKET_ERROR)
          wprintf(L"closesocket function failed with error: %ld\n", WSAGetLastError());
        WSACleanup();
        return;
      }
    }
    void Listen() {
      if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        wprintf(L"listen failed with error: %ld\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return;
      }
    }
    SOCKET GetSocket() {
      return sock;
    }
  private:
    sockaddr_in service;
    WSADATA wsaData;
    SOCKET sock;
  };
  struct Stream{
    Stream(SOCKET sock) : sock(sock) {}
    auto Read() {
      promise_t<int> awaiter;
      auto state = awaiter._state->lock();
      int iResult;
     
        iResult = recv(sock, recvbuf, recvbuflen, 0);
        /*if (iResult > 0)
          printf("Bytes received: %d\n", iResult);
        else if (iResult == 0)
          printf("Connection closed\n");
        else
          printf("recv failed: %d\n", WSAGetLastError());*/
      state->set_value(iResult);
      state->unlock();
      return awaiter.get_future();
    }
    auto Write(const char* sendbuff, std::size_t sz) {
      promise_t<int> awaiter;
      auto state = awaiter._state->lock();
      int iResult = send(sock, sendbuff, sz, 0);
      state->set_value(iResult);
      state->unlock();
      return awaiter.get_future();
    }
    const char* GetRecvBuffer() {
      return recvbuf;
    }
    auto Close() {
      promise_t<int> awaiter;
      auto state = awaiter._state->lock();
      int iResult = shutdown(sock, SD_SEND);
      int ret = closesocket(sock);
      state->set_value(iResult);
      state->unlock();
      return awaiter.get_future();
    }
  private:
    SOCKET sock;
    char recvbuf[MAXBUFF];
    int recvbuflen = MAXBUFF;
  };
  struct Listener {
    Socket m_socket;
    Listener(std::string address, unsigned short port) {
      m_socket.Bind(address.c_str(), port);
      m_socket.Listen();
    }
    auto Accept() {
        promise_t<SOCKET> awaiter;
        auto state = awaiter._state->lock();
        auto ret = ::accept(m_socket.GetSocket(), NULL, NULL);
        if (ret == INVALID_SOCKET)
          wprintf(L"accept failed with error: %ld\n", WSAGetLastError());

        awaiter._state->set_value(ret);
        return awaiter.get_future(); 
    }
  };
};

future_t<void> run() {
  Tcp::Listener listener{"127.0.0.1",800};

  while (true)
  {
    Tcp::Stream strm(co_await listener.Accept());
    if (co_await strm.Read() > 0) {
      string s = "HTTP / 1.1 200 OK\r\n"
        "Date : Mon, 27 Jul 2009 12 : 28 : 53 GMT\r\n"
        "Server : Apache / 2.2.14 (Win32)\r\n"
        "Last - Modified : Wed, 22 Jul 2009 19 : 15 : 56 GMT\r\n"
        "Content - Length : 273\r\n"
        "Content - Type : text / html\r\n"
        "Connection : Closed\r\n\r\n"
        "<html>"
        "<body>"
        "<h1>Hello, World!</h1>"
        "</body>"
        "</html>";
      if (co_await strm.Write(s.c_str(), 273) > 0) {
        co_await strm.Close();
      }
      
    }
  }
}

int main() {
  run();
}
