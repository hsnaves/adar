# Adar

## Synopsis

A tool to manipulate Alto's disk images.

## Getting started

### Prerequisites

Adar requires GCC and MAKE to be installed on the system.

### Building

To build Adar, simply type the following commands on the root directory of the Adar source code repository:

```sh
$ make
```

This will compile Adar with no debugging information and with some compiler optimizations enabled. To enable debugging information, the user can specify `DEBUG=1` in the command line above, such as:

```sh
$ DEBUG=1 OPTIMIZE=0 make
```



