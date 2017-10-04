# BrightFuture: A Simple Implementation of a Then()-able future

In the C++11 specification, a new concurrency library was added to the
standard library. It provides some convenient APIs like `std::async()`,
`std::future` and `std::promise`. However, an important feature was missing:
attaching a continuation routine when a future is fulfilled. The aim of
this small library is to fill that gap.

# Design Rationale

As this library is developed as a polyfill for [`std::experimental::future`](http://en.cppreference.com/w/cpp/experimental/future),
it has the following goals:

*   Using only existing standard C++ concurrency facilities.
*   Easy to deploy to existing C++ projects.
*   Small, single header only.
*   Works with common multi-threaded frameworks like boost::asio and Qt.
*   Extensible to any other multi-threaded frameworks.
*   Use an interfere similar to the upcoming Concurrency TS for easy
	migration.

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
	auto future = async([]
	{
		std::this_thread::sleep_for(2s);
		return 100;
	}, &exe);
	
	// Attach a continuation routine, which will be run when the
	// async task is finished. The argument of the continuation
	// routine is the return value of the async task (i.e. 100).
	// The continuation routine will be run in the thread specified
	// by the executor (i.e. sch).
	future.then([](int val)
	{
		assert(val == 100);
		return "abc"s;
	}, &exe).
	
	// The return value of then() is another future, which refers to
	// the return value of the previous continuation routine. We can
	// attach another continuation routine to this returned future
	// to be run after the previous continuation routine finishes.
	// The argument of this continuation routine is the return value
	// of the previous continuation routine.
	then([](const std::string& s)
	{
		assert(s == "abc"s);
	}, &exe);}
	
	// Quit the executor and the worker thread
	exe.Quit();
	worker.join();
}
```

# Requirement

A C++14 compiler and standard library.

C++14 is used in this project for:
*   [`auto` return value for functions](https://isocpp.org/wiki/faq/cpp14-language#generalized-return)
*   [Generalized lambda captures](https://isocpp.org/wiki/faq/cpp14-language#lambda-captures)

Both features are not mandatory for implementating futures. It just make
the code shorter and easier to read.

# Download

There is only one header file to download: [BrightFuture.hh](BrightFuture.hh).
