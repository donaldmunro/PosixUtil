#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch.hpp"

#include <thread>
#include <array>
//#include <latch> // C++20
#include "Latch.hh" // C++11 & 14 or use experimental latch
#include "Process.hh"
#include "TmpFile.hh"
#include "NamedSemaphore.hh"


void thread_run(std::shared_ptr<posix_util::Process> ptester_process, Latch* latch)
//----------------------------------------------------------------------
{
   sigset_t mask;
   sigemptyset(&mask);
   sigaddset(&mask, SIGCHLD);
   if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
   {
      perror("pthread_sigmask");
      std::cerr << "Masking signals on thread 2 failed" << std::endl;
      return;
   }

   posix_util::TmpFile stdout_file("stdout");
   std::stringstream ss;
   for (int i=1; i< 10; i++)
      ss << "Output " << i << std::endl;
   stdout_file.write(ss);
   stdout_file.close();
   std::vector<std::string> args = {"0", stdout_file.path(), "-", "500", "500"};
   ptester_process->async_execute(args, ptester_process, true, false);
   latch->decrement();
   while (ptester_process->status() != 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

TEST_CASE( "synchronous tests", "[sync]" )
{
   posix_util::Process tester_process("./cmake-build-debug/tester");

   SECTION( "Simple status check" )
   {
      std::vector<std::string> args = {  "42" };
      tester_process.sync_execute(args);
      REQUIRE( tester_process.status() == 42 );
      std::cout << "Simple status check complete" << std::endl;
   }
   SECTION( "Simple time out" )
   {
      posix_util::Process sleep_process("sleep");
      std::vector<std::string> args = {  "10" };
      sleep_process.sync_execute(args, false, false, 1000);
      REQUIRE(sleep_process.status() == std::numeric_limits<int>::min());
      sleep_process.kill();
      std::cout << "Simple time out complete" << std::endl;
   }
   SECTION( "stdout single line" )
   {
      posix_util::Process echo_process("echo");
      std::vector<std::string> args = {  "testing testing 1..2..3" };
      echo_process.sync_execute(args, true);
      REQUIRE(echo_process.raw_output() == "testing testing 1..2..3\n" );
      auto it = echo_process.output_begin();
      REQUIRE(*it == "testing testing 1..2..3" );
      REQUIRE(echo_process.status() == 0);
      std::cout << "stdout single line complete" << std::endl;
   }
   SECTION( "stdout,stderr multiple lines" )
   {
      posix_util::TmpFile stdout_file("stdout");
      posix_util::TmpFile stderr_file("stderr");
      std::stringstream ss;
      for (int i=1; i< 10; i++)
         ss << "Output " << i << std::endl;
      stdout_file.write(ss);
      ss.str("");
      for (int i=1; i< 10; i++)
         ss << "Error " << i << std::endl;
      stderr_file.write(ss);
      std::vector<std::string> args = {  "0",  stdout_file.path(), stderr_file.path() };
      stdout_file.close(); stderr_file.close();
      tester_process.sync_execute(args, true, true);
      int i = 1;
      for (auto it = tester_process.output_begin(); it !=tester_process.output_end(); ++it)
      {
         ss.str("");
         ss << "Output " << i++;
         REQUIRE(*it == ss.str());
      }
      i = 1;
      for (auto it = tester_process.error_begin(); it !=tester_process.error_end(); ++it)
      {
         ss.str("");
         ss << "Error " << i++;
         REQUIRE(*it == ss.str());
      }
      REQUIRE(tester_process.status() == 0);
      std::cout << "stdout,stderr multiple lines" << std::endl;
   }
}

TEST_CASE( "asynchronous tests", "[async]" )
{
   class TestProcess : public posix_util::Process
//--------------------------------------------------
   {
   public:
      explicit TestProcess(const std::string& pth) : posix_util::Process(pth), mutex("/TestProcess") {  mutex.create(false); }

      bool join(int timeout_ms =0)
      //----------------------------
      {
         if (! mutex.decrement(timeout_ms))
         {
            int err = mutex.last_error();
            if ( (timeout_ms > 0) && (err == ETIMEDOUT) )
               return false;
            if (err != EINTR)
               return false;
         }
         return true;
      }
   protected:
      void on_child_death() override {  mutex.increment(); /*std::cout << "Child " << pid << " died" << std::endl;*/ }
   private:
      posix_util::NamedSemaphore mutex;
   };

   SECTION( "Async death check" )
   {
      posix_util::NamedSemaphore::destroy("/TestProcess");
      std::shared_ptr<TestProcess> ptester_process = std::make_shared<TestProcess>("./cmake-build-debug/tester");
      std::vector<std::string> args = {  "42",  "-", "-", "1200" };
      ptester_process->async_execute(args, ptester_process);
      REQUIRE(ptester_process->join());
      REQUIRE( ptester_process->status() == 42 );
      posix_util::NamedSemaphore::destroy("/TestProcess");
      std::cout << "Async death check complete" << std::endl;
   }

   SECTION( "Async stdout/err" )
   {
      posix_util::TmpFile stdout_file("stdout");
      posix_util::TmpFile stderr_file("stderr");
      std::stringstream ss;
      for (int i=1; i< 10; i++)
         ss << "Output " << i << std::endl;
      stdout_file.write(ss);
      ss.str("");
      for (int i=1; i< 10; i++)
         ss << "Error " << i << std::endl;
      stderr_file.write(ss);
      stdout_file.close(); stderr_file.close();
      std::vector<std::string> args = {"0", stdout_file.path(), stderr_file.path(), "1000", "600"};
      std::shared_ptr<posix_util::Process> ptester_process = std::make_shared<posix_util::Process>("./cmake-build-debug/tester");
      REQUIRE(ptester_process->async_execute(args, ptester_process, true, true));
      while (ptester_process->is_alive())
      { // do stuff
         std::this_thread::sleep_for(std::chrono::milliseconds(300));
      }
      int i = 1;
      for (auto it = ptester_process->output_begin(); it !=ptester_process->output_end(); ++it)
      {
         ss.str("");
         ss << "Output " << i++;
//         std::cout << ss.str() << " " << *it << std::endl;
         REQUIRE(*it == ss.str());
      }
      i = 1;
      for (auto it = ptester_process->error_begin(); it !=ptester_process->error_end(); ++it)
      {
         ss.str("");
         ss << "Error " << i++;
//         std::cout << ss.str() << " " << *it << std::endl;
         REQUIRE(*it == ss.str());
      }
      REQUIRE( ptester_process->status() == 0 );
      std::cout << "Async stdout/err complete" << std::endl;
   }

   SECTION( "Async stdout/err with async read" )
   {
      posix_util::TmpFile stdout_file("stdout");
      posix_util::TmpFile stderr_file("stderr");
      std::stringstream ss;
      for (int i=1; i< 10; i++)
         ss << "Output " << i << std::endl;
      stdout_file.write(ss);
      ss.str("");
      for (int i=1; i< 10; i++)
         ss << "Error " << i << std::endl;
      stderr_file.write(ss);
      stdout_file.close(); stderr_file.close();
      std::vector<std::string> args = {"0", stdout_file.path(), stderr_file.path(), "1000", "600"};
      std::shared_ptr<posix_util::Process> ptester_process = std::make_shared<posix_util::Process>("./cmake-build-debug/tester");
      REQUIRE(ptester_process->async_execute(args, ptester_process, true, true));
      while (ptester_process->is_alive())
      {
         int bytes = ptester_process->async_read_stdout();
         bytes += ptester_process->async_read_stderr();
         if (bytes > 0)
            std::cout << "Output lines " << ptester_process->output_lc()
                      << ", Error lines " << ptester_process->error_lc() << " so far" << std::endl;
         std::this_thread::sleep_for(std::chrono::milliseconds(300));
      }
      int i = 1;
      for (auto it = ptester_process->output_begin(); it !=ptester_process->output_end(); ++it)
      {
         ss.str("");
         ss << "Output " << i++;
//         std::cout << ss.str() << " " << *it << std::endl;
         REQUIRE(*it == ss.str());
      }
      i = 1;
      for (auto it = ptester_process->error_begin(); it !=ptester_process->error_end(); ++it)
      {
         ss.str("");
         ss << "Error " << i++;
//         std::cout << ss.str() << " " << *it << std::endl;
         REQUIRE(*it == ss.str());
      }
      REQUIRE( ptester_process->status() == 0 );
      std::cout << "Async stdout/err with async read complete" << std::endl;
   }

   SECTION( "Async multithread" )
   {
      const unsigned int nt = std::thread::hardware_concurrency();
      posix_util::Process::set_child_death_handler();
      std::unique_ptr<Latch> latch = std::make_unique<Latch>(nt);
      std::vector<std::shared_ptr<posix_util::Process>> processes;
      for (unsigned int i=0; i<nt; i++)
      {
         std::shared_ptr<posix_util::Process> ptester_process =
               std::make_shared<posix_util::Process>("./cmake-build-debug/tester");
         std::stringstream ss;
         ss << "thread " << i;
         ptester_process->set_name(ss.str().c_str());
         processes.push_back(ptester_process);
         std::thread t = std::thread(thread_run, ptester_process, latch.get());
         t.detach();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
//      std::cout << "Outstanding " << posix_util::Process::async_outstanding() << std::endl;
      latch->wait();
//      std::cout << "Outstanding " << posix_util::Process::async_outstanding() << std::endl;
      int timeout = 60000;
      while (processes.size() > 0)
      {
         for (auto it = processes.begin(); it != processes.end();)
         {
            std::shared_ptr<posix_util::Process>  ptester_process = *it;
            if (! ptester_process->running())
            {
               REQUIRE(ptester_process->status() == 0);
               int i = 1;
               for (auto it = ptester_process->output_begin(); it !=ptester_process->output_end(); ++it)
               {
                  std::stringstream ss;
                  ss << "Output " << i++;
//                  std::cout << *it << " " << ss.str() << ptester_process->raw_output() << std::endl;
                  REQUIRE(*it == ss.str());
               }
               it = processes.erase(it);
            }
            else
               ++it;
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(300));
         timeout -= 300;
         if (timeout <= 0) break;
      }
      REQUIRE(timeout > 0);
      std::cout << "Async multithread complete" << std::endl;
   }
};