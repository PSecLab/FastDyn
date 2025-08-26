cd device_models/postmartem
python3 ./context_minimizer.py ~/data/qemu/build/io.log STM32F429
python3 ./prompt_gen.py GPIOG
cat ./prompt.txt
cd verifier
source ./runtest_elder.sh
cd ../../../
