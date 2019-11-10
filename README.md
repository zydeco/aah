# aah

**a**rm64 **a**rchitecture **h**andler.

It uses [unicorn](http://www.unicorn-engine.org) and [libffi](https://sourceware.org/libffi/) to run iOS arm64 binaries on x86_64 macOS, with varying degrees of success.

Most things will fail to launch because they need frameworks/symbols that aren't available on macOS.

## Requirements

* macOS 10.15

To run iOS apps, aah relies on the Mac Catalyst frameworks that are present on macOS 10.15.

## Running the sample app

The sample app is based on UIKitCatalog from the Apple sample code, with an integrated capstone disassembler to inspect functions and methods.

1. Build the `TestApp` target. This builds an arm64 iOS app (`TestApp.app`), and makes a copy with the Mach-O header changed to be emulatable (`TestApp-aah.app`, in the same output directory).
2. Edit the `aah-TestApp` scheme, select the `TestApp-app.app` executable by choosing “Other...” from the Executable drop-down menu (under Run > Info) and selecting it from the filesystem. Otherwise Xcode will complain that the target doesn't match the current platform.
3. Run the `aah-TestApp` scheme.

Steps 1 and 2 are only necessary before the first run.

## How it works

1. The architecture field of the binary is changed so macOS will load it.
2. `libaah.dylib` is inserted with `DYLD_INSERT_LIBRARIES`.
3. Upon load, it will detect which loaded binaries should be emulated, by looking at the `reserved` field in the header.
4. On emulated binaries, the executable sections are changed to be non-executable. This will cause an `EXC_BAD_ACCESS` exception when it's executed.
5. A signal handler is set to catch those exceptions, and emulate the code with unicorn.
6. An unicorn instance will be creatd on each thread if needed, with its address space mirroring the host, except for executable sections:
    1. Sections with arm64 code are marked as executable for unicorn.
    2. Native-executable sections (i.e. system libraries) are marked as non-executable for unicorn. This causes an exception when unicorn tries to execute them, which is used to return execution to the host.

### Transitions between native and emulated code

Transitions between host and emulated execution is handled by `libffi` at function entry/exit points, and requires the function signature to be known. This is done by keeping a mapping of entry points and their signatures (see `cif.c`).

The file `SymbolTable.plist` contains the signatures for supported functions (using [Objective-C type encoding](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/ObjCRuntimeGuide/Articles/ocrtTypeEncodings.html)), and it's added as a section to the `libaah.dylib` binary. The key `objc shims` is used by the `objc_msgSend` shim to call variadic methods. This file is added as a section to the `libaah.dylib` binary at build time.

When new entry points are found at runtime, they are added with `cif_cache_add` or `cif_cache_add_new`. This is used for Objective-C methods, pthreads, blocks and function pointers.

The format of the method signature determines how the call is handled:

1. A plain method signature will call the function by translating the arguments between the registers and stack of the host and emulator. This is enough for most functions.
2. `$` + shim name: A shim that will be called when emulated code calls the function. The shim is defined with the `SHIMDEF` macro, it receives an emulator context where it can access the registers, and can return `SHIM_RETURN` or an address to continue execution. Examples:
    * Variadic functions: `printf` or `NSLog` (see `nslog.m`).
    * Overriding functions with custom behaviour: `objc_msgSend`, `setjmp`.
3. `<` + method signature + `>` + wrapper name: Defines wrapper(s) that will be called after and/or before the native function is called. The wrappers are defined with the `WRAP_EMULATED_TO_NATIVE` and `WRAP_NATIVE_TO_EMULATED` macros, and have arguments `rvalue` and `avalues` that work like those of [`ffi_call`](https://www.chiark.greenend.org.uk/doc/libffi-dev/html/The-Basics.html). See `libdispatch.c` for examples.

## Preparing an app

You will need a thin non-encrypted arm64 app to start with.

### Repackaging

Repackage with this modified version of [marzipanify](https://github.com/zydeco/marzipanify).

This will do the following:

1. Repackage it as a macOS app.
2. Rename some linked libraries to the ones in `/System/iOSSupport`
3. Change the executable's architecture to x86_64.
4. Flag the executable so it's detected by `libaah` to be emulated (`0x456D400C` in the `reserved` field of the header).
5. Remove the `MH_PIE` flag (probably not be needed, but makes debugging easier).
6. Replace the `LC_VERSION_MIN_IPHONEOS` load command with `LC_BUILD_VERSION`, or update `LC_BUILD_VERSION`.
7. Resign the package.

### Libraries

Most binaries won't launch because of missing libraries or symbols. [optool](https://github.com/zydeco/optool) might help:

* weaken linked libraries and symbols
* rename linked libraries

After modifying the binary, it will have to be resigned:


If a library is present on macOS but missing some symbols, it's possible to build a stub library with the missing symbols, and make it export all of the original library by adding `-Xlinker -reexport_library /path/to/original.dylib` to its linker flags.

For functions in a native library to be called from emulated code, they must have an entry in `SymbolTable.plist`, as an [Objective-C method signature](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/ObjCRuntimeGuide/Articles/ocrtTypeEncodings.html). This can be automated with the `msdecl` tool from [CParser](https://github.com/zydeco/CParser), how to do this is somewhat documented in `SymbolTable/make_symbol_table.sh`.

If the app links to libc++, package it with the arm64 version included in `lib/libc++em.dylib`, and rename the linked library:

    optool rename /usr/lib/libc++.1.dylib @executable_path/../lib/libc++em.dylib --target /path/to/package.app/Contents/MacOS/executable

## Running

Run the patched executable inserting `libaah.dylib`:

    $ DYLD_INSERT_LIBRARIES=/path/to/libaah.dylib /path/to/executable

### Environment Variables

Some useful environment variables recognised by `aah`:

* `PRINT_DISASSEMBLY=1` will print disassembled instructions as they are executed by the emulator.
* `PRINT_REGS=1` will print the registers after and before function calls, or before printing each executed instruction (when combined with `PRINT_DISASSEMBLY`).

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

4. In the `aah` project, edit the `aah` scheme and choose an executable/app to debug.
5. Make sure the environment variables include `DYLD_INSERT_LIBRARIES` with `$(TARGET_BUILD_DIR)/libaah.dylib` first.
6. Build & run
7. It will hit a `SIGBUS` when it first encounters code to emulate. To prevent it, run this in the lldb console:
    
    There is already a shared breakpoint in the project that does this in `init_aah`.
    
    ```
    (lldb) process handle --pass true --stop false --notify true SIGBUS
    (lldb) continue
    ```

