#include <sys/stat.h>
#include <semaphore.h>

#include <string>

#ifndef _01FS2AD0P4DA2EQFZ8NQNAMF3W
#define _01FS2AD0P4DA2EQFZ8NQNAMF3W
namespace posix_util
{
   class NamedSemaphore
   //===================
   {
   public:
      explicit NamedSemaphore(const char* name);
      bool open();
      bool create(bool open_if_exists =true, mode_t mode = 0666, int count = 0);
      bool increment();
      bool decrement(int timeout_ms =0);
      bool close();
      bool destroy();
      int last_error() const { return last_err; }

      static int destroy(std::string name);

   private:
      std::string name;
      sem_t* semaphore;
      int last_err;
   };
}
#endif