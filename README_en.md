# mfm
Mains Frequency Measurement

The aim of the project is to measure the mains frequency accurately.

* This is purely a fun project,
* I don't earn my money from hardware development or embedded programming,
* some of my design decisions may therefore be suboptimal. :-)

In my case, “Linux PC” means a Fedora (currently Fedora 37). But the distro used doesn't matter.


## About the two basic ways to measure frequencies

1. You can open a gate for an exact time (e.g. exactly 1 second) and then count how many pulses were detected during this time. This is the "classic" frequency counting. This works excellently at high frequencies.

2. You can wait until the trigger is reached, then wait for the trigger to trigger again and determine the time difference between the two trigger events. In order to achieve high accuracy, this is done over a longer period of time, i.e. over several periods of the input signal.



For an exact measurement of a frequency of around 50 Hz, i.e. a very low frequency, with an accuracy of at least +/ 1mHz or rather better, method #2 is obviously the only suitable choice. With method #1 you would have to measure for a very long time in order to see enough pulses.

However, method #2 has a disadvantage. If you always want to measure the same number of periods, which I do, because 50 periods are always measured, then the measurement DURATION depends on the measurement VALUE. At a frequency above 50 Hz the measurement is shorter than one second. At a frequency below 50 Hz, longer. This is important to mention because many mathematical methods assume equidistant values. And the calculation of the grid time also requires measurements ​​every second.

## About crystals

There are different ways to generate a frequency of exactly 10 MHz, for example. Standard crystals, temperature-compensated crystals and crystals ovens (Oven Controlled Crystal Oscillator, OCXO) are the most common methods. A balance must be found between cost/effort and the requirements of the application. A standard crystal was too inaccurate for my taste. Standard crystals are quite temperature dependent. I could clearly see this temperature dependence in my experiments. An accuracy of +/- 1 mHz could not be achieved, IMO.


A temperature compensated crystal (Temperature Compensated Crystal Oscillator, TCXO) corrects the error by measuring the temperature of the crystal. The process is generally much more accurate than a standard crystal. But these are difficult to find.

A crystal oven keeps the temperature of the crystal at a constant level. The stability of the frequency essentially only depends on the accuracy of the temperature measurement and the control circuit. You can get the quartz oven I use for under 20 euros on eBay from China, including shipping. And for an OCXO that is very, very cheap. That's the reason why I didn't look further into the TCXO option.



[About the hardware](Hardware_en.md)

[About the Software](Software_en.md)
