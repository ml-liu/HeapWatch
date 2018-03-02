#all: SimplePreload_64.so SimplePreload_32.so
all: heapwatch_64.so

heapwatch_64.so: Preload.c StackStorage.c BoundedPriQueue.c
	gcc -Ifunhook/include -Ifunhook/distorm/include -g -shared -fPIC -fvisibility=hidden -m64 -Wno-missing-braces -D_GNU_SOURCE -DPFB_NO_EXTERNAL_FUNC -DPFB_MSVC_FORMAT  -o heapwatch_64.so  Preload.c StackStorage.c BoundedPriQueue.c  funhook/src/os_func.c funhook/src/os_func_unix.c funhook/src/funchook_syscall.S funhook/src/printf_base.c funhook/src/funchook.c funhook/src/funchook_io.c funhook/src/funchook_x86.c funhook/src/funchook_unix.c funhook/distorm/src/mnemonics.c funhook/distorm/src/wstring.c funhook/distorm/src/textdefs.c funhook/distorm/src/prefix.c funhook/distorm/src/operands.c funhook/distorm/src/insts.c funhook/distorm/src/instructions.c funhook/distorm/src/distorm.c funhook/distorm/src/decoder.c  -ldl -lpthread
	gcc -g demo.c -o demo

heapwatch_32.so: Preload.c StackStorage.c BoundedPriQueue.c
	gcc -Ifunhook/include -Ifunhook/distorm/include -g -shared -fPIC -fvisibility=hidden -m32 -Wno-missing-braces -D_GNU_SOURCE -DPFB_NO_EXTERNAL_FUNC -DPFB_MSVC_FORMAT  -o heapwatch_32.so  Preload.c StackStorage.c BoundedPriQueue.c  funhook/src/os_func.c funhook/src/os_func_unix.c funhook/src/funchook_syscall.S funhook/src/printf_base.c funhook/src/funchook.c funhook/src/funchook_io.c funhook/src/funchook_x86.c funhook/src/funchook_unix.c funhook/distorm/src/mnemonics.c funhook/distorm/src/wstring.c funhook/distorm/src/textdefs.c funhook/distorm/src/prefix.c funhook/distorm/src/operands.c funhook/distorm/src/insts.c funhook/distorm/src/instructions.c funhook/distorm/src/distorm.c funhook/distorm/src/decoder.c  -ldl -lpthread
	gcc -g demo.c -o demo
clean:
	rm -rf demo
	rm -rf *.o
	rm -rf *.so



