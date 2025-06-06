#pragma once
#include "3rdparty/include/cpp-httplib/httplib.h"

class HttpServer {
 public:
  HttpServer(int port);
  void start();

 private:
  int port_;
  httplib::Server server_;
  void setup_routes();
};
