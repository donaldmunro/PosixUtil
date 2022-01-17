#include <cstdlib>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <vector>
#include <cstring>
#include <unistd.h>

inline std::string trim(const std::string &str,  std::string chars  = " \t")
//----------------------------------------------------------------
{
   if (str.length() == 0)
      return str;
   auto b = str.find_first_not_of(chars);
   auto e = str.find_last_not_of(chars);
   if (b == std::string::npos) return "";
   return std::string(str, b, e - b + 1);
}

std::string read_file(char *filepath)
//-----------------------------
{
   std::stringstream ss;
   std::ifstream in(filepath);
   if (! in)
      ss << "File not found: " << filepath;
   else
      ss << in.rdbuf();
   return ss.str();
}

int main(int argc, char **argv)
//-------------------------------
{
   std::string output, error;
   std::vector<std::string> output_lines, error_lines;
   int sleepms = 0, lines_sleepms = 0, status = 0;
   char buf[8192];
//   for (int i=0; i<argc; i++)std::cout << argv[i] << " "; std::cout << std::endl;
   if (argc > 1)
   {
      std::string s = trim(argv[1]);
      if (s != "")
         status = std::stoi(s);
   }
   if ( (argc > 2) && (strcmp(argv[2], "-") != 0) )
   {
      char* prealpath = realpath(argv[2], buf);
      if (prealpath != nullptr)
         output = read_file(prealpath);
      else
         output = argv[2];
      if (trim(output) == "")
         output = "";
      else
      {
         std::stringstream ss(output);
         std::string line;
         while(std::getline(ss, line,'\n'))
            output_lines.push_back(line);
      }
   }
   if ( (argc > 3)  && (strcmp(argv[3], "-") != 0) )
   {
      char* prealpath = realpath(argv[3], buf);
      if (prealpath != nullptr)
         error = read_file(prealpath);
      else
         error = argv[3];
      if (trim(error) == "")
         error = "";
      else
      {
         std::stringstream ss(error);
         std::string line;
         while(std::getline(ss, line,'\n'))
            error_lines.push_back(line);
      }
   }
   if (argc > 4)
   {
      std::string s = trim(argv[4]);
      if (s != "")
         sleepms = std::stoi(s);
   }
   if (argc > 5)
   {
      std::string s = trim(argv[5]);
      if (s != "")
         lines_sleepms = std::stoi(s);
   }
   auto it1= output_lines.begin();
   auto it2= error_lines.begin();
   while (true)
   {
      if (it1 != output_lines.end())
      {
         std::cout << *it1 << std::endl << std::flush;
         ++it1;
      }
      if (it2 != error_lines.end())
      {
         std::cerr << *it2 << std::endl << std::flush;
         ++it2;
      }
      if ( (it1 == output_lines.end()) && (it2 == error_lines.end()) )
         break;
      std::this_thread::sleep_for(std::chrono::milliseconds(lines_sleepms));
//      int ret = usleep(lines_sleepms*1000);
   }
   if (sleepms > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepms));
   return status;
}