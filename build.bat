if not exist "build" (
	mkdir build
)

cd build
cmake ../ -G"Visual Studio 17 2022"

cd ..
pause