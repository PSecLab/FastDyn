


## Points where LLM failed

1. Finding the correct CRC value for the iomcu uart endpoint!
Solution: This can be solved once we add an agent on top and give it the access to the gdb because that's how i found the correct crc value.

2. Continuing the progress




## Don't forget to merge

- Models/APIs for SPI has been updated. There was a bug for assigning the correct cs_id to the bus instance, it was always 0. Now it should be correct.