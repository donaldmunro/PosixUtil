Posix Utility Classes
======================
See unit test file test.cc for examples.
#Process 
Used for executing child processes. Also supports async execution where the invoking process
can continue executing while the executed rocess runs. Note when using this mode a shared_ptr
to Process is required to ensure the child death handler can correctly handle cases where the
original reference no longer exists. In the async case (particularly when using multiple threads)
 it is also recommended that the child handler is set before creating processes, for example 
~~~~
posix_util::Process::set_child_death_handler();
std::shared_ptr<posix_util::Process> ptester_process =
               std::make_shared<posix_util::Process>("./cmake-build-debug/tester");
...
...
ptester_process->async_execute(args, ptester_process, true, false);               
~~~~

#NamedSemaphore
Abstracts a named Posix semaphore.

#TmpFile
Abstracts temporary file creation
