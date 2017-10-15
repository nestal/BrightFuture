# What is Promise and Future?

Promises and Futures were introduced in C++11 to aid multi-threaded
programming. Think of promise and futures are simplified producer and
consumer agents in a world of threads.

Perhaps an example will be easier to understand:

| Producer Thread                |                     Consumer Thread |
|:-------------------------------|:------------------------------------|
|`promise<int> p;`               |                                     |
|                                | `future<int> f = p.get_future();`   |
|                                | `int value = f.get();`              |
|                                | `// blocked`                        |
| `p.set_value(100);`            |                                     |
|                                | `// f.get()` returns 100.           |

Basically, a `future` refers to a value that will be available in the
future. This value is make available through a `promise`. As shown above,
the value is sent by `promise::set_value()`, and `future::get()` will
receive it. The purpose of a promise/future is to transfer the value
across different threads. This value is often called the _shared state_
because it is shared by different threads.

Promise and futures are supposed to be used once: you can only send one
value from a promise to a future. After calling `promise::set_value()`,
the promise will be _fulfilled_ and can't be set again. Similarly,
the future will become `invalid` after calling `future::get()`. Calling
`future::get()` will throw an exception. The association between a promise
and a future will break after use.

# Asynchronous Wait for Futures

The problem of `future::get()` is that it is synchronous: the consumer
thread will block until the producer calls `promise::set_value()`. Modern
application seldom has the luxury of blocking, so `future::get()` is seldom
useful to them. That is why the new Concurrency TS from the C++ committee
extents the future to support _asynchronous_ waits by `future::then()`.

`future::then()` allows attaching _continuation routines_ to a future. The
continuation routines will be called right after the future is ready, i.e.
the producer thread has just called `promise::set_value()`.