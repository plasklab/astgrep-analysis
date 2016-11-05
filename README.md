# astgrep-analysis
## Requirements
- LLVM (> 3.9) in your system

## How to build
```
mkdir build & cd build
cmake ..
make
```

## Usage
```
opt -load build/astgrep_analysis/libastgrep.so -astgrep <hoge.ll> > /dev/null
```
