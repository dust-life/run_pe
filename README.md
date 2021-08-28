## 使用Named_Pipe来传输，绕过cs 1mb限制运行run_pe
> 代码写的很low，将就用


![](https://github.com/dust-life/test/blob/main/run.png)

```
usage: run_pe pe_path -t Target_path -p Arguments
```
```
目前需要自行编译libpeconv.lib 运行库设置为:/MT arch:x64 放到src目录下即可
```
> https://github.com/hasherezade/libpeconv/wiki/Building-the-library

> https://docs.microsoft.com/en-us/windows/win32/ipc/named-pipe-server-using-completion-routines

> https://github.com/hasherezade/libpeconv/tree/master/run_pe

> https://github.com/CCob/SharpBlock/blob/master/upload.cna
