# BrightFuture: A Simple Implementation of a then()-able `future`

In the C++11 specification, a new concurrency library was added to the
standard library. It provides some convenient APIs like `std::async()`,
`std::future` and `std::promise`. However, an important feature was missing:
attaching a continuation routine when a future is fulfilled. The aim of
this small library is to fill that gap.

There is only one header file to download: [BrightFuture.hh](BrightFuture.hh).

# Design Rationale

As this library is developed as a drop-in replacement for [upcoming Concurrency TS](http://en.cppreference.com/w/cpp/experimental/future),
it has the following goals:

*   Portable:
	*   Using only existing standard C++ concurrency facilities.
	*   Use an interface similar to the upcoming Concurrency TS for easy
		migration.
*   Small:
	*   Single header only. Easy to deploy to existing C++ projects.
*   Scalable:
	*   Does not create threads internally. Will always use the threads
		created by the user. 
	*   Can specify exactly which thread (or thread pool) to run a certain
		asynchronous task or continuation routine.
*   Extensible to any other multi-threaded frameworks.
	*   New Executors can be created to customize the needs of other
		frameworks: e.g. Boost asio and Qt. 

## [Detailed Design](doc/Design.md)
## [Future/Promise Primer](doc/Primer.md)

# Usage

```C++
#include "BrightFuture.hh"

using namespace BrightFuture;

int main()
{
	// Creates an executor to run the callbacks
	QueueExecutor exe;
	
	// Spawn 3 threads to run the executor. These worker threads will
	// block until we call Quit() on the QueueExecutor.
	auto workers = exe.Spawn(3);
	
	// Run a task asynchronously. Returns a future to the result.
	auto fut = async([]
	{
		std::this_thread::sleep_for(2s);
		return 100;
	}, &exe);
	
	// Attach a continuation routine to the async task above.
	// It will be run when the async task is finished. The argument
	// of the continuation routine is the return value of the async
	// task (i.e. 100).
	// The continuation routine will be run in the thread specified
	// by the executor (i.e. QueueExecutor). It doesn't have to be
	// the same as the one that runs the asynchronous task.
	fut.then([](future<int> val)
	{
		assert(val.get() == 100);
		return "abc"s;
	}, &exe).
	
	// The return value of then() is another future, which refers to
	// the return value of the previous continuation routine. We can
	// attach another continuation routine to this returned future
	// to be run after the previous continuation routine finishes.
	// The argument of this continuation routine is the return value
	// of the previous continuation routine.
	then([](future<std::string> s)
	{
		assert(s.get() == "abc"s);
	}, &exe).
	
	// You can also wait synchronously for a future to be fulfilled.
	// Here in this example we want the main thread to block until
	// the continuation routines finishes, so we take the future
	// returned by the previous then() and call wait() on it.
	wait();
	
	// The threads are waiting for work to be submitted to the executor
	// (via async()). We call Quit() to instruct it to stop waiting.
	exe.Quit();
	
	// Now the worker threads should have already exited. We can safely
	// join() them without blocking.
	for (auto&& w : worker)
		w.join();
}
```

# Requirement

A C++14 compiler and standard library.

C++14 is used in this project for:
*   [`auto` return value for functions](https://isocpp.org/wiki/faq/cpp14-language#generalized-return)
*   [Generalized lambda captures](https://isocpp.org/wiki/faq/cpp14-language#lambda-captures)

Both features are not mandatory for implementating futures. It just make
the code shorter and easier to read.

# Limitations

There are a few features missing in BrightFuture that are supported by
`std::experimental::future`:

*   Unwrapping constructor: i.e. `future<T>::future( future<future<T>&& t)`
	and `when_all()` require an executor.
*   Future of references: i.e. `future<T&>`.

While it is not impossible to implement these features, doing so will
compromise the simple design. The library will become much bigger and
harder to maintain.

