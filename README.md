# 工具介绍
heapwatch工具基于内存分配函数的HOOK技术，当内存分配时，会记录分配该块内存的堆栈，将堆栈信息生成一个ID(如果两次调用内存分配堆栈一致，则只记录一次堆栈，返回的ID值相同)，并在返回的指针头部保存该ID值(分配内存时会多分配一部分内存用来记录heapwatch需要记录的数据)。同时会更新堆栈ID对应的统计信息(分配次数、分配的字节数、回收次数、回收的字节数)。

当测试完成时，向heapwatch发送命令让其将内存分配堆栈统计信息输出时，工具会将各个堆栈及其分配信息输出到heapwatch.dump文件，通过该文件可以获取到哪些调用堆栈分配了大量内存，且其是否存在较多的内存未释放，以此来协助开发快速定位内存泄漏。

目前该泄漏排查工具已应用于多个项目。


# 工具使用方法
heapwatch无需更改服务器代码，只需要在启动SERVER前，设置LD_PRELOAD环境变量，将heapwatch.so加载到服务器进程中即可。

步骤如下：
* 将heapwatch.so、symbol.py、dump_syms三个文件放到服务器进程可执行文件所在目录；
* 运行`LD_PRELOAD=./heapwatch.so ./Server`启动服务器(这里假定启动服务器的命令为./Server，若是其他命令，修改即可);
* 启动后输入`echo "help" | nc -U /tmp/heapwatch.sock`即可获取到与heapwatch的交互命令集;
* `echo "begin" | nc -U /tmp/heapwatch.sock`命令用来通知heapwatch.so开始收集内存分配统计信息(启动时默认不开始监控内存分配，因为heapwatch默认想监控服务器稳定运行后，在某个案列测试期间的内存增量);
* 当案例测试完成后，输入`echo "dump" | nc -U /tmp/heapwatch.sock`，此时会在服务器可执行文件目录生成`heapwatch.dump`文件；
* `heapwatch.dump`文件是文本文件，可以阅读，但里面的堆栈信息以模块名和地址来记录的，我们用symbol.py来将其解释为函数名及代码行，这里使用了google breakpad提供的dump_syms工具来实现;
* 在heapwatch.dump文件所在目录执行`python symbol.py heapwatch.dump heapwatch.out`，此时会解释`heapwatch.dump`为`heapwatch.out`文件，此文件中的堆栈会有函数名及行号等信息，可以用来做分析了；

# heapwatch.out统计信息阅读

```cpp
unfree size: 450570445
malloc count: 212062846
calloc count: 1161610
realloc count: 19171616
free count: 222021100
```
顶部信息中unfree size是监控时间段内分配未释放的内存。根据此内存大小来评估是否存在可能泄露。后续的信息则是各个代码路径分配释放内存统计，并将代码路径未释放的内存大小列出：
```cpp
ref count: 2254018, total alloc count: 3168984, total free count: 914966, unfree size 126225008,
, stack: 

```

ref count表示有2254018个内存块未释放，未释放的总大小为:126225008字节。目前统计文件的内存大小均以字节为单位。同时未释放的字节数从小到大来进行排序，文件尾部记录的堆栈为未释放内存字节数最多的堆栈。




# 说明
部分服务器进程可能无法使用该工具，此时麻烦通知我们，我们及时对其进行修改让其适配更多的服务器。
