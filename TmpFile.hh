#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <filesystem>

#ifndef cky8uqbfz0000ow35t8daqmyw
#define cky8uqbfz0000ow35t8daqmyw
namespace posix_util
{
   class TmpFile
   //============
   {
   public:
      explicit TmpFile(const char* prefix, std::string tempdir = std::filesystem::temp_directory_path().string());
      ~TmpFile();
      const char* path();
      const FILE* descriptor() { return fd; }

      void write(std::string s);
      void writeln(std::string s);
      void write(std::stringstream& ss);
      void flush();
      void close();

   private:
      std::filesystem::path file_path;
      FILE* fd;
   };
}
#endif