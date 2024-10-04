# Coomer

> Yet another zoomer application for Linux. This is an reimplementation of [Boomer](https://github.com/tsoding/boomer) in C++. I did this because Alexey Kutepov said ["better just rewrite this shit in C"](https://youtu.be/81MdyDYqB-A?t=669) 
>
> Well, it's C++ not C, but how bad that could be!

## Dependencies
```bash
sudo apt-get install build-essential libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libglu1-mesa-dev freeglut3-dev
```

## Build
```bash
g++ coomer.cpp -lGL -lGLU -lX11 -lXrandr -lXext -o coomer
```
