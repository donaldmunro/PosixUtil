#include <iostream>
#include "TmpFile.hh"

namespace posix_util
{
   TmpFile::TmpFile(const char* prefix, std::string tempdir)
   //-----------------------------------------------------------------------------------------------------------
   {
      std::stringstream ss;
      ss << prefix << "XXXXXX";
      file_path = std::filesystem::path(tempdir) / ss.str();
      int d = mkstemp(const_cast<char*>(file_path.c_str()));
      if (d == -1)
      {
         perror("mkstemp");
         fd = nullptr;
         return;
      }
      fd = fdopen(d, "w");
      if (fd == nullptr)
         perror("fdopen");
   }

   TmpFile::~TmpFile()
   //-----------------
   {
      if (fd != nullptr) { fclose(fd); fd = nullptr; }
      std::filesystem::remove(file_path);
//      std::cout << "deleted " << file_path.string() << std::endl;
   }
   const char* TmpFile::path()  { return file_path.c_str(); }
   void TmpFile::writeln(std::string s)
   {
      if (fd == nullptr)
         throw std::runtime_error("TmpFile not open");
      fprintf(fd, "%s\n", s.c_str());
   }

   void TmpFile::write(std::stringstream& ss)
   //----------------------------------------
   {
      if (fd == nullptr)
         throw std::runtime_error("TmpFile not open");
      auto len = ss.str().size();
      if (fprintf(fd, "%s", ss.str().c_str()) != len)
         throw std::runtime_error("TmpFile write error");
   }

   void TmpFile::write(std::string s)
   //----------------------------------
   {
      if (fd == nullptr)
         throw std::runtime_error("TmpFile not open");
      auto len = s.size();
      if (fprintf(fd, "%s", s.c_str()) != len)
         throw std::runtime_error("TmpFile write error");
   }
   void TmpFile::flush()
   //--------------------
   {
      if (fd == nullptr)
         throw std::runtime_error("TmpFile not open");
      fflush(fd);
   }
   void TmpFile::close() { fflush(fd); fclose(fd); fd = nullptr; }
}