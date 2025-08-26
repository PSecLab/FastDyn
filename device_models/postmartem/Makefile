all:
	python3 ./context_minimizer.py ./tests/io.log STM32F429  --method ngram --n 2

new:
	python3 ./context_minimizer.py /data/qemu/build/io.log STM32F429  --method ngram --n 2 --isr-window 10000000
