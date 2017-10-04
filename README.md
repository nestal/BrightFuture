# BrightFuture: A Simple Implementation of a then()-able future

In the C++11 specification, a new concurrency library was added to the
standard library. It provides some convenient APIs like `std::async()`,
`std::future` and `std::promise`. However, an important feature was missing:
attaching a continuation routine when a future is fulfilled. The aim of
this small library is to fill that gap.

There is only one header file to download: [BrightFuture.hh](BrightFuture.hh).

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

*   `shared_future::then()`: currently `BrightFuture::share()` returns
	an normal `std::shared_future` only. There is no `then()` for
	`std::shared_future`.
*   Unwrapping constructor: i.e. `future<T>::future( future<future<T>&& t)`

While it is not impossible to implement these features, doing so will
compromise the simple design. The library will become much bigger and
harder to maintain.

# Design

There are 3 major components in this library: _future_, _executor_ and
_tasks_. Futures keep track of the result of an asynchronous function call.
Tasks associate the argument, return values of these asynchronous calls in
one package. Executors keep track of the tasks and execute them.

Note that futures can be destroyed before their continuation routines
are executed. They are temporary objects that are design to be passed
around until someone attach a continuation routine to it. Afterwards,
they are not useful anymore and can be destroyed.

## Tasks

Suppose tasks are the easiest to explain. It contains a function object,
the argument and return value to that function object. The function object
is known when we create a task, so we just store it directly in the Task
object. However, the argument and return value are not known when a task
is created.

Tasks are used to represent continuation routines and asynchronous calls.
That means tasks are created only in `future::then()` and `async()`. When
a task represents a continuation routine, its argument is not known until
the previous async call finishes. That is why the argument in a task is
a `std::future`. We must make sure the argument is ready before executing
the task.

The return value of a task, on the other hand, is the output of the task.
It is an `std::promise` object, which will be `set_value()`'ed after
executing the task.

The task also contain a _token_ to the next continuation routines to be
called when this task finishes. Similarly, we don't know if there is
any continuation routine following this task until someone calls then().
Therefore by default the token will be null, and it means no continuation
routines will follow.

## Executors and Tokens

Executors have two purposes: to manage tasks and execute them. To manage
tasks, the executor assigns tokens to them. When someone wants something
to be execute later, it calls `Executor::Add()` to add the task to the
executor, and get a token referring to it. Although we know we want
something to be run later, we can't actually run it because we may not
know the argument to it. Think of when calling `future::then()`, we
know we have a continuation routine we want to execute later, but we
need to wait until the argument of the continuation routine is known,
that is, after the previous asynchronous call finishes.

That is why we have `Executor::Schedule()`. This is what we do when we
know the argument is ready and we can go ahead and execute the task.
The token is passed to the executor to specify which task to run now.

To fulfill the order purpose, executing tasks, the executor is typically
have a _main loop_, which is run by a number of threads. It typically
owns a queue of tasks waiting for to be run by some threads. BrightFuture
provides a `QueueExecutor` that implements a thread pool using
`std::thread`s. It is easy to implement another executor for boost::asio
or Qt.

## Futures

Unlike executors, tasks and tokens, futures are user-visible. The API of
Future is directly used by the developers. It is the most important, but
also very simple.

The futures in BrightFuture is just a simple wrapper around `std::future`.
The implementation of most member functions of BrightFutures just forwards
to `std::future`, except `then()`.

In order to allow developers to attach continuation routines, BrightFutures
also have a token to the continuation routine. By default, this token is
null, indicating no continuation routine to be run. It is set by `then()`.
