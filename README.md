# cpp-future-exercise

This is a basic implementation of std::future with support of continuations,
the aim of this implementation is to learn how std::future can be implemented.

I borrowed some ideas from libraries boost thread, folly, from-scratch, proposal of inplace function to C++ standard.

The tests can be compiled with C++17 and cmake and catch2 library,
in the directory where you downloaded/cloned the repository execute the commands

    mkdir lib

    put the catch2 library to lib directory

    mkdir build
    cd build
    cmake ..
    make

