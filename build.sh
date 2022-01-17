if [ ! -d "build/" ]; then
  mkdir build/
fi
if [ ! -f build/CMakeCache.txt ]; then
   cd build/
   cmake ..
  cd .. 
fi
cmake --build build/
