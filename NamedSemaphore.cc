#include "NamedSemaphore.hh"
#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <sstream>
#include <cstring>

namespace posix_util
{

   NamedSemaphore::NamedSemaphore(const char* sem_name)
   //--------------------------------------
   {
      if (sem_name[0] != '/')
         name = std::string("/") + sem_name;
      else
         name = sem_name;
      semaphore = SEM_FAILED;
      last_err = 0;
   }

   bool NamedSemaphore::open()
   //--------------------
   {
      semaphore = sem_open(name.c_str(), 0);
      if (semaphore == SEM_FAILED)
      {
         last_err = errno;
         perror("sem_open in open()");
         return false;
      }
      return true;
   }

   bool NamedSemaphore::create(bool open_if_exists, mode_t mode, int count)
   //------------------------------------------------------------------------------
   {
      semaphore = sem_open(name.c_str(), O_CREAT | O_EXCL, mode, count);
      if (semaphore == SEM_FAILED)
      {
         last_err = errno;
         if ( (last_err == EEXIST) && (open_if_exists) )
            return open();
         perror("sem_open in open_or_create()");
         return false;
      }
      return true;
   }

   bool NamedSemaphore::increment()
   //--------------------------
   {
      if (semaphore == SEM_FAILED)
      {
         std::stringstream errs;
         errs << "NamedSemaphore::increment(): Invalid/uninitialized semaphore " << name;
         std::cerr << errs.str() << std::endl;
         throw std::runtime_error(errs.str());
      }
      if (sem_post(semaphore) != 0)
      {
         last_err = errno;
         perror("increment");
         return false;
      }
      return true;
   }

   bool NamedSemaphore::decrement(int timeout_ms)
   //-----------------------------------------
   {
      if (semaphore == SEM_FAILED)
      {
         std::stringstream errs;
         errs << "NamedSemaphore::increment(): Invalid/uninitialized semaphore " << name << std::endl;
         std::cerr << errs.str() << std::endl;
         throw std::runtime_error(errs.str());
      }
      if (timeout_ms == 0)
      {
         if (sem_wait(semaphore) != 0) // block indefinitely
         {
            last_err = errno;
            return false;
         }
      }
      else if (timeout_ms > 0) // timed block
      {
         //struct timespec ts = { timeout_ms / 1000, (timeout_ms % 1000)*1000000 };
         time_t sec = timeout_ms / 1000;
         //long ns = (timeout_ms - sec*1000)*1000000;
         long ns =  (timeout_ms % 1000)*1000000;
         struct timespec ts;
         if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
         {
            last_err = errno;
            perror("clock_gettime");
            std::stringstream errs;
            errs << last_err << ": " << strerror(last_err);
            std::cerr << errs.str()  << std::endl;
            throw std::runtime_error(errs.str());
         }
         ts.tv_sec += sec;
         ts.tv_nsec = ns;
         assert(ts.tv_nsec >= 0);
         assert(ts.tv_nsec <= 999999999);
         int ret;
         while ((ret = sem_timedwait(semaphore, &ts)) == -1 && errno == EINTR) continue;
         if (ret != 0)
         {
            last_err = errno;
            if (last_err == ETIMEDOUT)
               return false;
            else
            {
               std::stringstream errs;
               errs << last_err << ": " << strerror(last_err) << " " << sec << "." << ns;
               std::cerr << errs.str()  << std::endl;
               throw std::runtime_error(errs.str());
            }
         }
      }
      else // return immediately
      {
         if (sem_trywait(semaphore) != 0)
         {
            last_err = errno;
            if (last_err == EAGAIN)
               return false;
            else
            {
               std::stringstream errs;
               errs << last_err << ": " << strerror(last_err);
               std::cerr << errs.str()  << std::endl;
               throw std::runtime_error(errs.str());
            }
         }
      }
      return true;
   }

   bool NamedSemaphore::close()
   //---------------------
   {
      if (semaphore == SEM_FAILED)
      {
         std::stringstream errs;
         errs << "NamedSemaphore::increment(): Invalid/uninitialized semaphore " << name;
         std::cerr << errs.str() << std::endl;
         throw std::runtime_error(errs.str());
      }
      if (sem_close(semaphore) != 0)
      {
         last_err = errno;
         return false;
      }
      semaphore = SEM_FAILED;
      name = "";
      return true;
   }

   int NamedSemaphore::destroy(std::string name)
   //----------------------------------------
   {
      return sem_unlink(name.c_str());
   }

   bool NamedSemaphore::destroy()
   //-----------------------
   {
      last_err = destroy(name);
      if (last_err != 0)
         return false;
      return true;
   }
}
