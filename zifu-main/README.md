# zifu

libch347.so放到板子/usr/lib下
ch347——lib.h放到你开发库或者demo的同级目录


执行编译：
gcc -O2 -Wall font5x7_demo.c -o font5x7_demo -lch347
sudo ./font5x7_demo /dev/hidraw1

