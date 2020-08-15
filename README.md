# libvirttime

libvirttime provides transparent time virtualization on Linux, all in userspace.
Specifically, libvirttime applies a user-defined offset to absolute time values
crossing the libc boundary.

This library is somewhat obsoleted by time namespaces. However, the library
requires no privileges.

This library pairs well with
[libvirtcpuid](https://github.com/twosigma/libvirtcpuid), which allow to
virtualize CPUID and force the `LD_PRELOAD` variable thanks to its environment
variable injection capability.

## Problem description

Applications have access to numerous clocks on the system. Some of these clocks
are not synchronized across machines. We have measured instances where the clock
`CLOCK_MONOTONIC` differs by more than 6 months on different machines at a given
time. Because this clock cannot be adjusted by the machine administrator,
migrating an application across machines can get problematic. The following
example illustrates the problem.

Consider a Java application sleeping for a second via `Thread.sleep(1000)`
that we want to migrate from host A to host B. The Java runtime implements the
sleep function as such:

```
now = get_current_time(CLOCK_MONOTONIC);
pthread_cond_timedwait(..., now+1000);
```

After migration, if host B has a clock greater than host A, then the
`pthread_cond_timedwait` function immediately returns upon resume as the clock
jumped into the future. This behavior is fairly harmless, and happens when
applications get temporarily suspended. On the other hand, if host B has a clock
smaller than host A, then upon resume, the application would continue sleeping
more than anticipated. If the clock difference happens to be a month, then
the application would sleep for a month instead of a second, hanging the
application.

To deal with this issue, we wish to apply an offset to all absolute time values
that the application perceives and emits. In userspace, interposing on the libc
functions that accept absolute time arguments is good enough for our purposes.

## Usage

1. Compile with `make` to produce the `libvirttime.so` library.
2. Create the configuration file (see format below).
3. Run application with `LD_PRELOAD=libvirttime.so VIRT_TIME_CONF=/path/to/conf app`

### Configuration file

We need to be able to adjust the time offset during a migration. This implies
that we cannot invoke library code when adjusting the offset. To deal with
this issue, libvirttime memory maps a configuration file that can be modified
during a migration.

The configuration file is in binary format and has the following structure:

```c
struct config {
	struct timespec ts_offset;
	struct timespec per_thread_ts[NUM_PID];
};
```

* The `ts_offset` field is applied as an offset to all absolute time arguments
  of `CLOCK_MONOTONIC` and `CLOCK_BOOTTIME` clock types.

* The `per_thread_ts` array is used when a thread makes a libc function call.
  During the libc function invocation, libvirttime puts the time argument in
  that array, so that upon migration, one can adjust offsets for in-flight
  function calls.

We provide a Python script to generate the configuration file and adjust
the offset during migration. See `config-example.py`. We also provide a
Rust source code. See `config-example.rs`.
Note that the configuration file size is 64Mb (4194304 possible pids), but has
holes, so it doesn't take much space on disk.

## Limitations

* We have not implemented offsets on `CLOCK_REALTIME`. For the migration use
  case, this is not an issue as this clock can be approximately synchronized across
  machines.
* The system calls `sys_futex()`, `sys_timer_gettime()`, `sys_timer_settime()`
  are not virtualized but should be. Our applications do not use them, so we
  haven't bothered interposing these.

## License

The code is licensed under the Apache 2.0 license.
