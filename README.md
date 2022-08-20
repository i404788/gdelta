# Description
This is a delta compression algorithem developed by Haoliang Tan, the first author of [Exploring the Potential of Fast Delta Encoding: Marching to a Higher Compression Ratio](https://ieeexplore.ieee.org/abstract/document/9229609/) .

As written in the abstract section of the paper above, Gdelta is better than Xdelta:
>Our evaluation results driven by six real-world datasets suggest that Gdelta achieves encoding/decoding speedups of 2X∼4X over the classic Xdelta and Zdelta approaches while increasing the compression ratio by about 10%∼120%.

# How to install
```
mkdir -p build && cd build
cmake ..
make -j
make install # as root/sudo
```

Now it can be used as library (`libgdelta.a`), or as standalone application (through `gdelta.exe`).

Bindings are available for python3/pybind11: <https://github.com/i404788/gdelta-python>

# Author
Written based by [Ferris Kwaijtaal](https://github.com/i404788) & [Wang Haitao](https://github.com/apple-ouyang), based on work by [HaoLiang Tan](https://github.com/AnsonHooL).

HaoLiang has another more complex version of gdelta using zstd, you can see [here](https://github.com/AnsonHooL/gdelta).
