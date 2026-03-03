omc export_fmu.mos
cd output_folder/249.fmutmp/sources
mkdir build
cd build
cmake -DCMAKE_C_FLAGS="-g -O0" -DCMAKE_BUILD_TYPE=Debug ..
make 
cd ../../../../
python3 ../autogen_boilerplate.py ./output_folder/249.fmutmp/ -o ./harness/fmu_header.h
