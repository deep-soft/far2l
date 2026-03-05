mkdir -p _release && cd _release
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DPYTHON=yes .. 
cmake --build . 

hdiutil create -volname "far2l-release" -srcfolder "install/far2l.app" -ov -format UDZO -o "far2l-release.dmg"
