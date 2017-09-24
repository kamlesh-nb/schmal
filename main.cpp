


#include <iostream>
#include <thread>
#include "schmal.h"
#include "awaitio.h"

using namespace awaitio;
using namespace schmal;

struct handler {
  async_mr_event_t event;
  int value;
  void producer(int number)
  {
    value = number;
    event.set();
  }
  task consumer()
  {
    co_await event;
    ++value;
    ++value;
  }
};


int main() 
{
  schmal::start();
  return 0;
}
