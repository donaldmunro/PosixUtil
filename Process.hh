#include <string>
#include <vector>
#include <iostream>
#include <ostream>
#include <filesystem>
#include <unordered_map>
#include <csignal>
#include <functional>
#include <atomic>
#include <mutex>

#ifndef _6c7d81a9037040a79526937efd1d5c63
#define _6c7d81a9037040a79526937efd1d5c63
namespace posix_util
{
   class Process
   //=============
   {
      public:
         explicit Process(const char* pth) : Process(std::string(pth)) {};
         explicit Process(const std::string& pth);
         Process(const Process& other) = delete;
         Process(const Process&& other) = delete;

         bool sync_execute(std::vector<std::string>& args, bool is_stdout = false, bool is_stderr = false,
                           int timeout_ms = 0);
         bool async_execute(std::vector<std::string>& args, const std::shared_ptr<Process>& me,
                             bool is_stdout = false, bool is_stderr = false);
         bool is_alive();
         bool running() const { return is_running; }

         int async_read_stdout();
         int async_read_stderr();
         void async_custom_child_death_handler(std::function<void(int, siginfo_t *si, void *)>& f);

         std::string get_filepath() const { return filepath.parent_path().string(); }
         std::string get_filename() const { return filepath.filename(); }
         void set_name(const char* nme) { extra_name = nme; }
         std::string get_name() { return extra_name; }
         int last_error() const { return last_err; }
         int status() const { return last_status; }
         pid_t get_pid() const { return pid; }
         std::string last_error_message() const { return last_error_mess; }
         std::string raw_output() { return stdout_raw; }
         std::string raw_error() { return stderr_raw; }
         std::vector<std::string>::iterator output_begin();
         std::vector<std::string>::iterator output_end() { return stdout_lines.end(); }
         std::size_t output_lc();
         std::vector<std::string>::iterator error_begin();
         std::vector<std::string>::iterator error_end() { return stderr_lines.end(); }
         std::size_t error_lc();
         int kill();
         int read_all_after_death();

         friend std::ostream& operator<<(std::ostream& ostr, const Process &o);

         static int timed_waitpid(pid_t pid, int timeout_ms);
//         static bool nonblocking(int pipe);
         static int read_stream(int pipe, std::string& raw);
         static int async_read_stream(int pipe, std::string& raw, int timeout_ms=0);
         static void default_child_death_handler(int signal, siginfo_t* info, void * context);
         static void set_child_death_handler(void (*handler)(int, siginfo_t*, void *) = nullptr);
         static std::string trim(const std::string &str,  std::string chars  = " \t");
         static std::size_t split(std::string s, std::vector<std::string>& tokens, std::string delim);
         static int async_outstanding();
         static int async_poll(std::vector<std::shared_ptr<Process>>& completed);

         static std::unordered_map<pid_t, std::shared_ptr<Process>> outstanding_pids;
         static std::mutex child_handler_mutex;
         static std::mutex  outstanding_mutex;
         static std::atomic_bool has_child_handler;
         static void (*chain_handler)(int, siginfo_t*, void *);

   protected:
         virtual void on_child_death() {}

         std::filesystem::path filepath;
         std::string extra_name;
         bool is_search_path;
         pid_t pid;
         int stdout_pipe, stderr_pipe;
         std::string stdout_raw, stderr_raw;
         std::vector<std::string> stdout_lines, stderr_lines;   
         int last_status, last_err;
         std::string last_error_mess;
         bool is_running;
         std::function<void(int, siginfo_t *si, void *)> custom_async_child_death;

      private:
         bool fork_exec(std::vector<std::string>& args, bool is_stdout, bool is_stderr, int& stdout, int& stderr);
   };
}
#endif