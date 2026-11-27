# Parena 

You need a parena to malloc. It is a single thread and quick memory allocate project.


## quick start

```
gcc nob.c -o build/nob
./build/nob run test/single/*.c
```

## How to use

Gdb mode
```
./build/nob -d run test/single/*.c
```

Only build
```
./build/nob -d build test/single/*.c
```


## Thanks

- [ngnix](https://github.com/nginx/nginx)
- [nob.h](https://github.com/tsoding/nob.h)

