//[hello
#include "binlog/binlog.hpp"
//]

#include <fstream>
#include <iostream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <initializer_list>
#include <set>

//[hello

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  std::string buf(reinterpret_cast<const char*>(data), size);
  buf.push_back('\0');
  
  
  std::set<int> set{4,8,15,16,23,42};
  std::map<std::string, std::string> map{{buf, buf}};
  BINLOG_INFO("Associative containers: {} {}", set, map);

// Outputs: Associative containers: [4, 8, 15, 16, 23, 42] [(a, alpha), (b, beta)]
 // BINLOG_INFO(buf.c_str());

  std::ofstream logfile("hello.blog", std::ofstream::out|std::ofstream::binary);
  binlog::consume(logfile);
//]

  if (! logfile)
  {
    //std::cerr << "Failed to write hello.blog\n";
    return 1;
  }

  //std::cout << "Binary log written to hello.blog\n";
  return 0;

//[hello
}
//]
