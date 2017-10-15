# What is Promise and Future?

Promises and Futures were introduced in C++11 to aid multi-threaded
programming. Think of promise and futures are simplified producer and
consumer agents in a world of threads.

Perhaps an example will be easier to understand:

| Producer                       |                            Consumer |
|:-------------------------------|:------------------------------------|
|`promise<int> p;`               |                                     |
|                                | `future<int> f = p.get_future();`   |
|                                | `int value = f.get();`              |
|                                | (blocked)                           |
| `p.set_value(100);`            | (`f.get()` returns 100.)            |
