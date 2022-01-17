#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <mutex>
#include <thread>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <cstring>

#include "Process.hh"

namespace posix_util
{
   void (*Process::chain_handler)(int, siginfo_t*, void *) = nullptr;
   std::unordered_map<pid_t, std::shared_ptr<Process>> Process::outstanding_pids{};
   std::atomic_bool Process::has_child_handler{false};
   std::mutex Process::child_handler_mutex{}, Process::outstanding_mutex{};

   Process::Process(const std::string& pth)
   //--------------------------------------
   {
      char buf[8192];
      stdout_raw.clear(); stderr_raw.clear();
      stdout_lines.clear(); stderr_lines.clear();
      stdout_pipe = stderr_pipe = -1;
      last_status = -1;
      last_err = 0;
      last_error_mess = "";
      pid = -1;
      is_running = false;
      filepath.clear();
      is_search_path = false;
      custom_async_child_death = nullptr;
      char* prealpath;
      if ( (pth.find_last_of('/') == std::string::npos) && (pth.find_last_of('\\') == std::string::npos) )
      {
         std::filesystem::path pp = std::filesystem::path(".") / pth;
         if (std::filesystem::exists(pp))          
            filepath = pp;
         else
         {
            is_search_path = true;
            filepath = pth;
         }
      }
      if (! is_search_path)
      {
         prealpath = realpath(pth.c_str(), buf);
         if (prealpath == nullptr)
         {
            last_err = errno;
            last_error_mess = "File not found";
            perror("realpath");
            filepath.clear();
            return;
         }
         filepath = std::string(prealpath);
         if (! std::filesystem::exists(filepath))
         {
            last_err = EEXIST;
            last_error_mess = "File not found";
            filepath.clear();
            return;
         }
      }
   }

   bool Process::sync_execute(std::vector<std::string>& args, bool is_stdout, bool is_stderr, int timeout_ms)
   //-----------------------------------------------------------------------------------------------------
   {
      pid = -1;
      stdout_raw.clear(); stderr_raw.clear();
      stdout_pipe = stderr_pipe = -1;
      stdout_lines.clear(); stderr_lines.clear();
      last_status = -1;
      if (filepath.empty())
      {
         last_err = -99;
         last_error_mess = "Path to executable not specified or not found.";
         return false;
      }
      if (! fork_exec(args, is_stdout, is_stderr, stdout_pipe, stderr_pipe))
         return false;
      if (is_stdout) 
      {
         read_stream(stdout_pipe, stdout_raw);
         close(stdout_pipe);
      }
      if (is_stderr) 
      {
         read_stream(stderr_pipe, stderr_raw);
         close(stderr_pipe);
      }
      int wstatus;
      if (timeout_ms <= 0)
         waitpid(pid, &wstatus, 0);
      else
         wstatus = timed_waitpid(pid, timeout_ms);
      if (wstatus == std::numeric_limits<int>::min())
         last_status = wstatus;
      else if (WIFEXITED(wstatus))
         last_status = WEXITSTATUS(wstatus);
      return (last_status == 0);
   }

   bool Process::async_execute(std::vector<std::string>& args, const std::shared_ptr<Process>& me,
                                bool is_stdout, bool is_stderr)
   //------------------------------------------------------------------------------------------------------------------------
   {
      pid = -1;
      stdout_raw.clear(); stderr_raw.clear();
      stdout_pipe = stderr_pipe = -1;
      stdout_lines.clear(); stderr_lines.clear();
      last_status = -1;
      if (! me)
      {
         last_err = -98;
         last_error_mess = "Null shared_ptr for me parameter";
         return false;
      }
      if (filepath.empty())
      {
         last_err = -99;
         last_error_mess = "Path to executable not specified or not found.";
         return false;
      }
      set_child_death_handler(&default_child_death_handler);
      if (! fork_exec(args, is_stdout, is_stderr, stdout_pipe, stderr_pipe))
         return false;
      std::lock_guard<std::mutex> lock(Process::outstanding_mutex);
      Process::outstanding_pids[pid] = me;
#ifdef __DEBUG__
      std::cout << "async_execute: " << pid << " " << this->extra_name << " started" << std::endl;
#endif
/*
      if ( (is_stdout) && (! nonblocking(stdout_pipe)) )
      {
         last_err = -99;
         last_error_mess = "Could not set stdout pipe to non-blocking.";
         return false;
      }
      if ( (is_stderr) && (! nonblocking(stderr_pipe)) )
      {
         last_err = -99;
         last_error_mess = "Could not set stderr pipe to non-blocking.";
         return false;
      }
*/
      return true;
   }

   int Process::async_read_stdout()  { return async_read_stream(stdout_pipe, stdout_raw); }

   int Process::async_read_stderr()  { return async_read_stream(stderr_pipe, stderr_raw); }

   bool Process::is_alive()
   //----------------------
   {
      if ( (! is_running) || (pid < 0) ) return false;
      int wstatus;
      if (waitpid(pid, &wstatus, WNOHANG) == pid)
      {
         if (WIFEXITED(wstatus))
            last_status = WEXITSTATUS(wstatus);
         is_running = false;
         read_all_after_death();
         on_child_death();
         std::lock_guard<std::mutex> lock(Process::outstanding_mutex);
         auto it = Process::outstanding_pids.find(pid);
         if (it != Process::outstanding_pids.end())
            Process::outstanding_pids.erase(it);
         return false;
      }
      int status = ::kill(pid, 0);
      if (status == 0) return true;
      return false;
   }

   int Process::kill()
   //-------------------
   {
      ::kill(pid, SIGTERM);
      int wstatus = timed_waitpid(pid, 500);
      if (wstatus == std::numeric_limits<int>::min())
      {
         ::kill(pid, SIGINT);
         wstatus = timed_waitpid(pid, 500);
         if (wstatus == std::numeric_limits<int>::min())
         {
            ::kill(pid, SIGKILL);
            return wstatus;
         }
      }
      return wstatus;
   }

   bool Process::fork_exec(std::vector<std::string>& args, bool is_stdout, bool is_stderr,
                           int& stdoutt, int& stderrr)
   //---------------------------------------------------------------------------------------
   {
      stdoutt = stderrr = -1;
      bool is_pipe = ( (is_stdout) || (is_stderr) );
      last_error_mess = ""; last_err = 0;
      int stdout_pipes[2], stderr_pipes[2];
      if (is_pipe)
      {
         if (is_stdout)
         {
            if ((last_err = pipe(stdout_pipes)) == -1)
            {
               perror("pipe");
               last_error_mess = "Creating pipe for stdout";
               return false;
            }
         }
         if (is_stderr)
         {
            if ((last_err = pipe(stderr_pipes)) == -1)
            {
               perror("pipe");
               last_error_mess = "Creating pipe for stderr";
               return false;
            }
         }
      }
      pid = fork();
      if (pid == -1)
      {
         perror("fork");
         last_err = errno;
         last_error_mess = "Fork failed";
         return false;
      }
      else if (pid == 0)  // Child
      {
         if (is_stdout)
         {
            while ((dup2(stdout_pipes[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
            close(stdout_pipes[1]);
            close(stdout_pipes[0]);
         }
         if (is_stderr)
         {
            while ((dup2(stderr_pipes[1], STDERR_FILENO) == -1) && (errno == EINTR)) {}
            close(stderr_pipes[1]);
            close(stderr_pipes[0]);
         }
         std::vector<char*> commandVector;
         commandVector.push_back(const_cast<char*>(filepath.filename().c_str()));
         for (auto it = args.begin(); it != args.end(); ++it)
            commandVector.push_back(const_cast<char*>((*it).c_str()));
         commandVector.push_back(NULL);
         char **command = commandVector.data();
         if (is_search_path)
            execvp(filepath.c_str(), &command[0]);
         else
            execv(filepath.c_str(), &command[0]);
         perror("sync_execute");
         _exit(1);
      }
      //parent
      is_running = true;
      if (is_stdout)
      {
         close(stdout_pipes[1]);
         stdoutt = stdout_pipes[0];
      }
      if (is_stderr)
      {
         close(stderr_pipes[1]);
         stderrr = stderr_pipes[0];
      }
      return true;
   }

   int Process::timed_waitpid(pid_t pid, int timeout_ms)
   //----------------------------------------
   {
      int wstatus = std::numeric_limits<int>::min();
      while (waitpid(pid, &wstatus, WNOHANG) == 0)
      {
         if (timeout_ms < 0)
            break;
         std::this_thread::sleep_for(std::chrono::milliseconds(100)); //usleep(100000);
         timeout_ms -= 100;
      }
      return wstatus;
   }

   int Process::async_read_stream(int pipe, std::string& raw, int timeout_ms)
   //-----------------------------------------------------------------
   {
//#ifdef __LINUX__
//      int n;
//      if (ioctl(pipe, FIONREAD, &n) != -1)
//         if (n <= 0) return 0;
//      else
//         perror("ioctl");
//#endif
      struct pollfd fds[1];
      fds[0].fd = pipe;
      fds[0].events = POLLIN;
      fds[0].revents = 0;
      ssize_t count = 0;
      int ret = poll(fds, 1, timeout_ms);
      if ( (ret > 0) && ( (fds[0].revents & POLLIN) || (fds[0].revents & POLLHUP) ) )
      {
         char buffer[4096];
         count = read(pipe, buffer, sizeof(buffer));
         if (count == -1)
         {
            int err = errno;
            if ((err == EINTR) || (err == EAGAIN)) return 0;
//            std::cerr << strerror(err) << std::endl;
            perror("read");
            return -1;
         }
         else if (count == 0) return 0;
         buffer[count] = 0;
         raw.append(buffer);
      }
      else
         count = 0;

      return count;
   }


   int Process::read_all_after_death()
   //-----------------------------
   {
      int n = 0;
      if (stdout_pipe >= 0)
      {
         n = read_stream(stdout_pipe, stdout_raw);
         stdout_lines.clear();
         split(stdout_raw, stdout_lines, "\n");
      }
      if (stderr_pipe >= 0)
      {
         n += read_stream(stderr_pipe, stderr_raw);
         stderr_lines.clear();
         split(stderr_raw, stderr_lines, "\n");
      }
      return n;
   }

   std::vector<std::string>::iterator Process::output_begin()
   //--------------------------------------------------------
   {
      stdout_lines.clear();
      split(stdout_raw, stdout_lines, "\n");
      return stdout_lines.begin();
   }

   std::size_t Process::output_lc()
   //------------------------------
   {
      stdout_lines.clear();
      split(stdout_raw, stdout_lines, "\n");
      return stdout_lines.size();
   }

   std::vector<std::string>::iterator Process::error_begin()
   //------------------------------------------------------
   {
      stderr_lines.clear();
      split(stderr_raw, stderr_lines, "\n");
      return stderr_lines.begin();
   }

   std::size_t Process::error_lc()
   //-----------------------------
   {
      stderr_lines.clear();
      split(stderr_raw, stderr_lines, "\n");
      return stderr_lines.size();
   }

   int Process::read_stream(int pipe, std::string& ss)
   //--------------------------------------------------------
   {
      char buffer[4096];
      int no = 0;
      while (1) 
      {
         ssize_t count = read(pipe, buffer, sizeof(buffer));
         if (count <= 0) break;
         no += count;
         buffer[count] = 0;
         ss.append(buffer);
      }
      return no;
   }

   std::ostream& operator<<(std::ostream& ostr, const Process& o)
   //---------------------------------------------------------------
   {
      std::stringstream ss;
      ss << "Process[path = " << o.get_filepath() << " name = " << o.get_filename() << ", status =" << o.last_status
         << " last_error = " << o.last_error() << " (" << o.last_error_message() << ")]";
      return ostr << ss.str();
   }

   void Process::default_child_death_handler(int signal, siginfo_t* info, void * context)
   //------------------------------------------------------------------------
   {
      pid_t spid = info->si_pid;
      if (signal != SIGCHLD)
      {
         std::cerr << "Received non-child signal " << signal << " from " << spid << std::endl;
         return;
      }
      int wstatus = timed_waitpid(spid, 500);
      do //do loop in case of batched signals
      {
         std::lock_guard<std::mutex> lock(Process::outstanding_mutex);
         auto it = Process::outstanding_pids.find(spid);
         if (it != Process::outstanding_pids.end())
         {
            std::shared_ptr<Process> sp = it->second;
            if (sp)
            {
               Process* me = sp.get();
               if ( (me != nullptr) && (me->is_running) )
               {
#ifdef __DEBUG__
                  std::cout << "Received SIGCHLD for " << spid << " " << me->get_name() << ", status"
                  << (((wstatus != std::numeric_limits<int>::min()) && (WIFEXITED(wstatus))) ? WEXITSTATUS(wstatus)
                                                                                                : wstatus) << std::endl;
#endif
                  me->is_running = false;
                  if (me->custom_async_child_death)
                  {
                     me->custom_async_child_death(signal, info, context);
                     return;
                  }
                  if (wstatus == std::numeric_limits<int>::min())
                  {
                     ::kill(spid, SIGTERM);
                     std::this_thread::sleep_for(std::chrono::milliseconds(150));
                     int wstatus2 = timed_waitpid(spid, 500);
                     if (wstatus2 == std::numeric_limits<int>::min())
                        ::kill(spid, SIGKILL);
                     else
                        wstatus = wstatus2;
                  }
                  if (WIFEXITED(wstatus))
                     me->last_status = WEXITSTATUS(wstatus);
                  else if (wstatus == std::numeric_limits<int>::min())
                  {
                     me->last_status = wstatus;
                     me->kill();
                  }
                  else
                     me->last_status = wstatus;
                  me->read_all_after_death();
                  me->on_child_death();
               }
            }
            Process::outstanding_pids.erase(it);
         }
         if (chain_handler != nullptr)
            (*chain_handler)(signal, info, context);
         spid = waitpid(-1, &wstatus, WNOHANG);  // Cater for batched signals
//         if (spid > 0) std::cout << "Received batched signal for " << spid << std::endl;
      }  while (spid > 0);
   }

   void Process::set_child_death_handler(void (*handler)(int, siginfo_t*, void *))
   //-------------------------------------------------------------------------
   {
      bool expected = false;
      Process::has_child_handler.compare_exchange_strong(expected, true);
      if (expected) return;
      struct sigaction last_action;
      if (handler == nullptr) handler = &default_child_death_handler;
      if (sigaction(SIGCHLD, nullptr, &last_action) == -1)
      {
         perror("sigaction (check last)");
         last_action.sa_sigaction = nullptr;
      }
      if (last_action.sa_sigaction == handler)
         return;
      else if (last_action.sa_sigaction != nullptr)
         chain_handler = last_action.sa_sigaction;

      struct sigaction action;
      sigemptyset(&action.sa_mask);
      sigaddset(&action.sa_mask, SIGCHLD);
      action.sa_flags = SA_SIGINFO;
      action.sa_sigaction = handler;
      if (sigaction(SIGCHLD, &action, nullptr) == -1)
      {
         perror("sigaction");
         exit(1);
      }
   }

   std::size_t Process::split(std::string s, std::vector<std::string>& tokens, std::string delim)
   //-----------------------------------------------------------------------------------
   {
      tokens.clear();
      std::size_t pos = s.find_first_not_of(delim);
      while (pos != std::string::npos)
      {
         std::size_t next = s.find_first_of(delim, pos);
         if (next == std::string::npos)
         {
            tokens.emplace_back(trim(s.substr(pos)));
            break;
         }
         else
         {
            tokens.emplace_back(trim(s.substr(pos, next-pos)));
            pos = s.find_first_not_of(delim, next);
         }
      }
      return tokens.size();
   }

   std::string Process::trim(const std::string &str,  std::string chars)
   //----------------------------------------------------------------
   {
      if (str.length() == 0)
         return str;
      auto b = str.find_first_not_of(chars);
      auto e = str.find_last_not_of(chars);
      if (b == std::string::npos) return "";
      return std::string(str, b, e - b + 1);
   }

   int Process::async_outstanding() { std::lock_guard<std::mutex> lock(Process::outstanding_mutex); return Process::outstanding_pids.size();  }

   int Process::async_poll(std::vector<std::shared_ptr<Process>>& completed)
   //------------------------------------------------------------------
   {
      int n = 0;
      std::lock_guard<std::mutex> lock(Process::outstanding_mutex);
      for (auto it=Process::outstanding_pids.begin(); it!=Process::outstanding_pids.end(); )
      {
         auto pp = *it;
         pid_t pid = pp.first;
         int wstatus;
         if (waitpid(pid, &wstatus, WNOHANG) != pid)
         {
            ++it;
            continue;
         }
         n++;
         std::shared_ptr<Process> sp = pp.second;
         if (sp)
         {
            if (WIFEXITED(wstatus))
               sp->last_status = WEXITSTATUS(wstatus);
            sp->is_running = false;
            sp->read_all_after_death();
            sp->on_child_death();
            completed.push_back(sp);
         }
         it = Process::outstanding_pids.erase(it);
      }
      return n;
   }

   void Process::async_custom_child_death_handler(std::function<void(int, siginfo_t*, void*)>& f)
   //-----------------------------------------------------------------------------------------------
   {
      custom_async_child_death = f;
   }

/*
   bool Process::nonblocking(int pipe)
   //-------------------------------------------
   {
      int flags = fcntl(pipe, F_GETFL);
      if (flags < 0)
      {
         perror("fcntl F_GETFL");
         return false;
      }
      if (fcntl(pipe, F_SETFL, flags | O_NONBLOCK) == -1)
      {
         perror("fcntl F_SETFL");
         return false;
      }
      return true;
   }
*/
};
