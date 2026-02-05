This firmware is from offical stm repo and uses both dma and adc.
This is to show how we can use dma as well.

# Two different models
There are two models provided, based on whether or not you want fuzzing
The fuzzing model implements similar funcitonality to an anchor, communicating with the fuzzer to fuzz on the model size rather than the firmware side.

If using the fuzzing model, make sure to include the provided anchor in the config file so fastdyn knows we want to run the fuzzer, though our model will be doing the actual work.