yes | cp -rf overwrites/lang/CMakeLists.txt dependencies/supercollider/lang
yes | cp -rf overwrites/lang/LangPrimSource/OSCData.cpp dependencies/supercollider/lang/LangPrimSource
yes | cp -rf overwrites/lang/LangPrimSource/PyrPrimitive.cpp dependencies/supercollider/lang/LangPrimSource
yes | cp -rf sclang/WebSocket dependencies/supercollider/SCClassLibrary/Common


mkdir build
cd build 
cmake ../dependencies/supercollider
make -j8
sudo make install

