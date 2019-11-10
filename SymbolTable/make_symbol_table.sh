#!/bin/sh

echo "actually this is not a real shell script, some things should be done manually"
exit

SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk

# size_t is Q
# unsigned long is Q
# ptrdiff_t, long is q
# long long is q, unsigned long long is Q

# libobjc
# manually add compiler objc runtime
msdecl -w -I- -I$SDKROOT/usr/include $SDKROOT/usr/lib/libobjc.A.tbd $SDKROOT/usr/include/objc/{runtime,objc,message,objc-exception,objc-sync}.h

# libsystem
# manually:
# blocks runtime
# cxxabi.h
# sys/acl.h doesn't get ssize_t, why?
# mpool.h
# sys/rbtree.h
# rpc/rpc.h
# htons/htonl/ntohl/ntohs
# simd/*.h, complex.h, tgmath.h?
# os/log,signpost
msdecl -w -I- -I$SDKROOT/usr/include $SDKROOT/usr/lib/libSystem.B.tbd $SDKROOT/usr/include/{unistd,stdlib,cache,cache_callbacks,copyfile,removefile,CommonCrypto/CommonCrypto,xattr_flags,dispatch/dispatch,mach-o/{dyld,arch,getsect,swap},mach/{mach,clock,clock_reply,exc,mach_init,mach_time,mach_traps,mach_voucher,port_obj},dlfcn,asl,sys/{_types,syslog,termios,stat,statvfs,ipc,timeb,mount,cdefs,sysctl,utsname,_endian,aio,clonefile,xattr,snapshot,fsgetpath,ioctl,mman,msg,timex,quota,uio,semaphore,sem,shm,qos},runetype,ctype,wctype,db,dirent,arpa/inet,netinet/in,net/{if_dl,ethernet,if},time,stdio,printf,execinfo,libgen,signal,nl_types,ndbm,ttyent,utmpx,err,fmtmsg,fnmatch,util,fts,ftw,getopt,glob,search,inttypes,langinfo,vis,spawn,regex,readpassphrase,locale,stringlist,monetary,time,utime,ulimit,dns_sd,rpc/rpc,fstab,resolv,ifaddrs,grp,rpcsvc/{yp,yppasswd},math,fenv,malloc/malloc,notify,libkern/{OSAtomic,OSAtomicDeprecated,OSSpinLockDeprecated,OSAtomicQueue,OSCacheControl},pthread/{pthread,sched},sandbox,libunwind,os/{activity,trace,lock},Block}.h

# Foundation
msdecl -w -I- -I$SDKROOT/usr/include $SDKROOT/System/Library/Frameworks/Foundation.framework/Foundation.tbd foundation.h

# Copy framework headers to framework_includes, so #include <Framework/Header.h> works like C

# CoreFoundation
msdecl -w -I- -I$SDKROOT/usr/include -Iframework_includes $SDKROOT/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation.tbd $SDKROOT/System/Library/Frameworks/CoreFoundation.framework/Headers/CoreFoundation.h

# CoreGraphics
msdecl -w -I- -I$SDKROOT/usr/include -Iframework_includes $SDKROOT/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics.tbd $SDKROOT/System/Library/Frameworks/CoreGraphics.framework/Headers/CoreGraphics.h

# Metal
msdecl -w -I- -I$SDKROOT/usr/include -Iframework_includes $SDKROOT/System/Library/Frameworks/Metal.framework/Metal.tbd framework_includes/Metal/Metal.h

# SC
msdecl -w -I- -I$SDKROOT/usr/include -Iframework_includes $SDKROOT/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration.tbd framework_includes/SystemConfiguration/SystemConfiguration.h

# Security
msdecl -w -I- -I$SDKROOT/usr/include -Iframework_includes $SDKROOT/System/Library/Frameworks/Security.framework/Security.tbd framework_includes/Security/Security.h framework_includes/Security/SecureTransport.h framework_includes/Security/SecProtocolObject.h
