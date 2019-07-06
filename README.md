# aah

**a**rm64 **a**rchitecture **h**andler.

It uses [unicorn](http://www.unicorn-engine.org) and [libffi](https://sourceware.org/libffi/) to run arm64 binaries on x86_64, with varying degrees of success.

Most things will fail to launch because they need frameworks/symbols that aren't available on macOS.

## Preparing an executable

You can do this automatically with my aah-aware fork of marzipanify, or manually like this:

1. Replace the architecture in the mach-o header with x86_64:

    ```
    $ echo "00000004: 0700 0001 0300" | xxd -r - /path/to/executable
    ```

2. Add magic  marker `0x456D400C` to the reserved field of the mach-o header:

    ```
    $ echo "0000001c: 0C40 6D45" | xxd -r - /path/to/executable
    ```

3. Change the platform to macOS or iOSMac (in the `LC_BUILD_VERSION` load command):
    ```
    $ ggrep -obUaP "\x32\x00\x00\x00\x20\x00\x00\x00\x02\x00\x00\x00\x00" /path/to/executable | head -n 1 | cut -d: -f1 | xargs printf "%08x: 32000000 20000000 06000000 000E0A00" | xxd -r -g 4 - /path/to/executable
    ```
    This command might need adjustments depending on your executable.

4. Resign the binary with the right entitlements.
    ```
    $ codesign --force --sign - --entitlements Entitlements.plist --timestamp=none SomeAPp.app
    ```

## Running

Run the patched executable inserting `libaah.dylib`:

    ```
    $ DYLD_INSERT_LIBRARIES=/path/to/libaah.dylib /path/to/executable
    ```

## Debugging

To debug, you'll need a custom build of debugserver that doesn't catch `EXC_BAD_ACCESS` exceptions, as this prevents them from being caught as signals in libaah:

1. Download the llvm source:

    ```
    $ git clone git@github.com:llvm/llvm-project.git
    ```

2. Patch `lldb/tools/debugserver/source/MacOSX/MachTask.mm`:

    ```diff
    diff --git a/lldb/tools/debugserver/source/MacOSX/MachTask.mm b/lldb/tools/debugserver/source/MacOSX/MachTask.mm
    index 6aa4fb23754..6148b628119 100644
    --- a/lldb/tools/debugserver/source/MacOSX/MachTask.mm
    +++ b/lldb/tools/debugserver/source/MacOSX/MachTask.mm
    @@ -601,7 +601,7 @@ bool MachTask::StartExceptionThread(DNBError &err) {
    
         // Set the ability to get all exceptions on this port
         err = ::task_set_exception_ports(
    -        task, m_exc_port_info.mask, m_exception_port,
    +        task, m_exc_port_info.mask & ~EXC_MASK_BAD_ACCESS, m_exception_port,
             EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, THREAD_STATE_NONE);
         if (DNBLogCheckLogBit(LOG_EXCEPTIONS) || err.Fail()) {
           err.LogThreaded("::task_set_exception_ports ( task = 0x%4.4x, "
    ```

3. Build debugserver with Xcode and install to `/Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework/Versions/A/Resources/debugserver`. You might want to save a backup of your original debugserver.

4. In the `aah` project, select Edit scheme and choose an executable to debug.
5. Make sure the environment variables include `DYLD_INSERT_LIBRARIES` with `$(TARGET_BUILD_DIR)/libaah.dylib` first.
6. Build & run
7. It will hit a `SIGBUS` when it first encounters code to emulate. To prevent it, run this in the lldb console:
    
    There is already a shared breakpoint in the project that does this in `init_aah`.
    
    ```
    (lldb) process handle --pass true --stop false --notify true SIGBUS
    (lldb) continue
    ```

