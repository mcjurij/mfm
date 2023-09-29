
## Software

The software consists of 4 components. An embedded SW for the ATmega and Pico W respectively. A server that receives the data that comes in over WiFi. And a simple tool for graphical output of the data.

### Embedded

To transfer the cross-compiled SW to the MCUs, two completely different methods are used. The ATmega is programmed using an In System Programmer (ISP). The Pico W has a button. If it is held when the Pico W is turned on, the Pico W goes into a special boot sequence (which cannot be overwritten). This allows you to mount a “mass storage” via USB and copy a .uf2 file there. The Pico W recognizes this and then transfers it to its flash memory and start the newly flashed programm.

I can imagine that the Pico W can be used to program the ATmega. I didn't follow this due to lack of time. It would have the advantage that you don't need an extra ISP. However, because of the 3.3V I/O voltage of the Pico, you need a driver (also called a level shifter or logic converter) for implementation and I see a space problem. However, you probably wouldn't have it if you took a smaller representative of the ATmel series (i.e. ATtiny). The ATmega328p is clearly oversized for this application. ATmega/ATtiny also run with 3.3V, but I don't know whether that's enough for 10Mhz (I doubt it). And no more than 3.3 + 0.5 V can be applied to the inputs of the ATmega/ATtiny.


#### Atmel ATmega328p

The source code for the ATmega is here:
![ATmega 328 src](embedded/ATmega328 "ATmega 328 src")

There is only one file: `main.c`. Since the ATmega only determines the time stamps between the edges of the square wave signal and sends them to the Pico W via UART, not much happens here. The 1PPS signal is used to set the timestamp counter to 0.

In order for the Makefile to run, the avr-gcc toolchain and the avrdude tool must be installed to flash it. If the ISP fits the one in the Makefile and is connected, you can compile and flash in one go with `make program`.


#### Raspberry Pico W


The source code for the Pico W is here:
![Pico W src](embedded/Pico_W/mfm "Pico W src")

I don't use any OS. The programmer determines what should run on the two cores of the Pico Ws, core0 and core1. My choice is that the reading of the UART messages and processing of the timestamps coming from the ATmega is done on core1. Communication with the server on core0 (main() is started there).

The WiFi chip uses interrupts that are wired to Core0 and therefore the WiFi code can only run on core0.

In order to determine an exact time, the Pico W fetches the time from mfm_server when it starts. This returns the time of the host computer, which is inaccurate, but is not a problem as long as it is accurate enough to determine the correct second (the host computer usually gets its time via NTP. The method depends on the network latency). This is now compared with the 1PPS signal and this gives you a very precise time.
I don't use the Pico's clock (RTC). The Pico's standard crystal requires permanent time correction, no matter how you do it. The Pico has a counter that counts microseconds once started. To this value I add the microseconds since January 1, 1970 midnight (UTC). An interrupt routine that is linked to the 1PPS signal checks every second how many microseconds the counter needs to be corrected (+ microseconds since January 1, 1970 midnight (UTC)).
Since the time stamps of the ATmega are also synchronized with the 1PPS signal, the measurement time can be determined very precisely (at least in theory). As soon as a measurement has taken place, only the information in which second the measurement was taken comes from the Pico and the microsecond (start/end of the measurement) from the ATmega. The time of the measurement is halfway between the start and end of the measurement. It is important to note that the start or end of the measurement does not have to be on a second barrier and that a measurement can be longer (or shorter) than one second.

- `conf.h` - Configuration like MAINS_FREQ
- `freq.c` - Receives timestamp from ATmega and calculates frequency from it. Runs on core1.
- `main.c` - contains main() which starts on core0, starts core1. On core0, receives the data written from `freq.c` to `ringbuffer.c` and sends it to the mfm_server.
- `ntime.c` - Determine the exact time by combining time information from the Linux PC and 1PPS.
- `proto.c` - protocol definition.
- `tcp_client.c` - Communication with the mfm_server. The lwip call-back API is _very_ fiddly.
- `ringbuffer.c` - Saving measured values ​​if network connection (briefly) not available.

First of all, set up the C SDK. https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html#sdk-setup
At https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html#quick-start-your-own-project you can find which Linux packages need to be installed.


Git clone, submodule init and set up environment variable.
```
git clone https://github.com/raspberrypi/pico-sdk.git

cd pico-sdk
git submodule update --init

export PICO_SDK_PATH=$HOME/<path to pico sdk>/pico-sdk/
```

Change to Pico W directory, cmake call.

```
cd embedded/Pico_W/

mkdir build
cd build

cmake -DPICO_BOARD=pico_w -DWIFI_SSID="<your SSID>" -DWIFI_PASSWORD="<your password>" ..
```

That should lead to the output below. I have blacked out my paths here.
![cmake Ausgabe](photos/cmake_run.png "cmake")

To build `make -j9`. The UF2 file used to flash is under `embedded/Pico_W/build/mfm/mfm_pico_w.uf2` (747 kb).


### On the Linux PC

The mfm_server runs on a Linux PC and accepts the data that comes from the Pico Ws via WiFi, processes it and writes it into files. These can be read by a GUI, the “Binge Watcher” or mfm_bwatcher.

The output made with printf() is transferred via USB. To do this, enter `minicom -C minicom.log -b 115200 -o -D /dev/ttyACM0` on the Linux PC after the Pico W has started. The transfer of output via USB sometimes hangs for me (after several hours or days). But the Pico W still runs.

After starting the Pico W, an attempt is made to connect to the WiFi (the SSID and password are passed on when calling cmake, as shown above), and if successful, time synchronization with the Linux PC is carried out. After that time stamps from the ATmega are processed. If nothing "unusual" happens this will be printed about every 4 seconds:
```
Interrupt handler count msg: XXX
```
Where XXX is the number of timestamps already processed.


#### mfm_server

The mfm_server should build directly with a `make`.
The server starts one thread per connection, i.e. per Counter. The two threads then pass the measured values ​​to a collector thread, which writes out the data for each counter and also combines them.

The mfm_server is not (yet) a real server; it lacks so-called daemonizing, which includes decoupling from the terminal. Currently there is also no way to shut down the server cleanly with a control program.

- `conn_slots.c` - each Pico W gets its own connection slot, uses the Pico ID to recognize that the same Pico W has connected.
- `file_mgr.c` - File management and rotation.
- `process_data.c` - Data evaluation.
- `proto.c` - Protocol definition, exact copy of the file in the embedded/Pico_W/mfm directory.
- `server.c`- Contains main(), accepts network connections and starts one thread per Pico W.

The Picos have a unique ID, which can be queried with https://github.com/mcjurij/mfm/blob/85a41c4bb33529b7771d14aba3f1979061e204ab/embedded/Pico_W/mfm/main.c#L51. In the mfm_server it is used in particular to determine the file names for the Counter-related files. After a Pico W has established a connection to the mfm_server, it first sends its ID.

##### Output data files

In addition to mfm_server's own log file `log.txt`, various files are written and continuously appended. If a file comes from a specific Counter, the file name contains the Pico ID. In any case the current date. With an example day, I'll take 2023-09-25, this quickly becomes clear. The first column of all files there is always has a time, either in us-Epoch or Epoch. The us-Epoch are microseconds since January 1, 1970 midnight (UTC, a 16-digit number). The Epoch are seconds since January 1, 1970 midnight (UTC). My Pico Ws have the IDs Counter 1: E661A4D41723262A, Counter 2: E661A4D41770802F.


| File name                                | Contents      |
| ---------------------------------------- | ------------- |
|`meas_data_E661A4D41723262A_2023-09-25.txt`|Measurements from Counter 1 with us-Epoch time|
|`meas_data_E661A4D41770802F_2023-09-25.txt`|Measurements from Counter 2 with us-Epoch time|
|`meas_data_local_E661A4D41723262A_2023-09-25.txt`|Measurements form Counter 1 with local time|
|`meas_data_local_E661A4D41770802F_2023-09-25.txt`|Measurements form Counter 2 with local time|
|`meas_sgfit_E661A4D41723262A_2023-09-25.txt`|Measurements form Counter 1 with us-Epoch time, with interpolation & Savitzky-Golay filter|
|`meas_sgfit_E661A4D41770802F_2023-09-25.txt`|Measurements form Counter 2 with us-Epoch time, with interpolation & Savitzky-Golay filter|
|`meas_sgfit_local_E661A4D41723262A_2023-09-25.txt`|Measurements form Counter 1 with lokaler time, with interpolation & Savitzky-Golay filter|
|`meas_sgfit_local_E661A4D41770802F_2023-09-25.txt`|Measurements form Counter 1 with lokaler time, with interpolation & Savitzky-Golay filter|
|`meas_merge_2023-09-25.txt`|merged measurements form Counter 1 & 2 with us-Epoch time, with interpolation|
|`meas_merge_sgfit_2023-09-25.txt`|merged measurements form Counter 1 & 2 with us-Epoch time, with interpolation & Savitzky-Golay filter|
|`gridtime_2023-09-25.txt`|Grid time with Epoch time|
|`gridtime_local_2023-09-25.txt`|Grid time with local time|
|`incidents_E661A4D41723262A_2023-09-25.txt`|Incidents form Counter 1|
|`incidents_E661A4D41770802F_2023-09-25.txt`|Incidents form Counter 2|

Interpolation is simply interpolating linearly between two measured values ​​in such a way that one value per second is obtained . The Savitzky-Golay filter (in this form) can only work with equidistant values. With `meas_merge_*` the average is taken from the two interpolations of Counters 1 and 2. With `meas_merge_sgfit_*`, Counters 1 & 2 are first interpolated, then the Savitzky-Golay filter is applied and then the average of these two values ​​is taken.

The grid time is calculated from the measured values ​​written to `meas_merge_sgfit_*`. In `gridtime_2023-09-25.txt` the Epoch time is in the first column. These are the seconds since January 1, 1970 midnight (UTC). In the second column is the grid time offset.

You can see the effect the Savitzky-Golay filter on the measured values ​​in the mfm_bwatcher, for example when loading `meas_data_E661A4D41723262A_2023-09-25.txt` _and_ `meas_sgfit_E661A4D41723262A_2023-09-25.txt`. Implementation see https://github.com/mcjurij/mfm/blob/5b119eb627beb55587a3f4324e777724062568a9/mfm_server/process_data.c#L553

There are various things in the mfm_server log file `log.txt`: messages about time synchronization, messages about file rotation, messages about implausibly long intervals between 2 measurements, messages about received incidents.

I'll write something about incidents below.


##### Start with grid time

When starting the mfm_server, the "official" grid time from e.g. https://www.swissgrid.ch/de/home/operation/grid-data/current-data.html#wide-area-monitoring (please scroll down) can be used as a command line argument.
The value of "Aktuelle Netzzeitabweichung:" ("current grid time deviation:") is the difference between grid time minus the current time.

E.g. 
```
./mfm_server 15.23
```
starts the mfm_server so that instead of 0, 15.23 seconds is used as the grid time offset at the time of start. This is not perfectly accurate, as there is a pause between the start of the program and the start of our own grid time calculation, but it is sufficient.


#### mfm_bwatcher

A simple tool for graphical output of data, similar to a function plotter, but with a few special features for this application. Qt 6 must be installed. I'm currently using Qt 6.4.3 and Qt Creator.
To configure the project go to File->Open File or Project, then load `mfm_bwatcher.pro`, then under "Configure Project" just select "Desktop ..." and click on the "Configure Project" button. Then click on the green triangle “Run” at the bottom left. This will build and start the mfm_bwatcher GUI.

The mfm_bwatcher only understands the files that have a us-Epoch or Epoch time (such as `meas_data_E661A4D41770802F_2023-09-25.txt`), not those with local time (such as `meas_data_local_E661A4D41770802F_2023-09-25.txt`). The ones with local time are intended for other plotters such as gnuplot. The file for the grid time (e.g. `gridtime_2023-09-25.txt` see above) may only have the Epoch time in the first column.

![Binge Watcher Follow Mode](photos/bwatcher_1.png "Binge Watcher Follow Mode")

This screenshot shows the data from Counter 1: `meas_data_E661A4D41723262A_2023-09-27.txt` and `meas_sgfit_E661A4D41723262A_2023-09-27.txt`. You can see the effect of the Savitzky-Golay filter.
In "Follow mode" the files from the mfm_server are always read again at the end and the graph is updated every second. The follow mode can be recognized by the box with the value and the arrow pointing to the right y-axis (called AxisTag in the source code).

![Binge Watcher Follow Mode](photos/bwatcher_2.png "Binge Watcher Follow Mode")

This screenshot shows the data of Counter 1: `meas_sgfit_E661A4D41723262A_2023-09-27.txt` and Counter 2: `meas_sgfit_E661A4D41770802F_2023-09-27.txt`. The two curves lie very precisely on top of each other, as the Savitzky-Golay filters almost completely smooth out the differences. It has to be said that the differences between the measurements from Counters 1 and 2 are very small anyway and are only really relevant for ripple control signals.

The time on the x-axis is always local time.

#### Menu structure

- File->Read measurements - read all files that start with 'meas_*` except those with local time.
- File->Read grid time - read the grid time offset `gridtime_*`, but not with local time.
- File->Read incidents - read the incidents, must match the selected `meas_*` file.
- File->Remove incidents - remove the incidents.
- File->Quit - exit the program.

Graphs can be deleted with a right click, after clicking on the graph or picking one in the legend.

When loading grid time you may have to zoom out along the y-Axis to see anything.

- View->Follow mode - switches to follow mode with the actual time range (see line with "Range:" at the bottom left).
- View->Follow mode 5 mins - switches to follow mode with a 5 min time range.
- View->Follow mode 15 mins - switches to follow mode with a 15 min time range.
- View->Go to... - here you can jump to any point in time and set the time range in minutes.
- View->Jump to PoI - here you can jump to the first, last, previous, next point of interest (PoI).

Points of interest are currently only frequencies above 50.1 or below 49.9. These are searched when reading the first `meas_*` file. Or for the first graph if several are displayed and you select

- Analyze->Find PoIs in measurement #1

They are also searched when you remove the first graph in the list of graohs.

- Help->Interactions - short explanation.
- Help->About Qt - Qt's standard dialog "About Qt".


### Incidents

When the Pico W evaluates the time stamps from the ATmega, it can happen that abnormalities are detected. This then leads to an incident. Whether there is an incident is checked in the source code from here on:
https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L476

There can be multiple incidents caused by one measurement. The main reason for the incident mechanism is that I want to be able to detect if a Counter delivers poorer measurements over time, due to aging processes, or for whatever reason.


#### ERROR: measurement failed

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L481

#### ERROR: rise measurement failed

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L498

#### ERROR: fall measurement failed

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L513

#### NOTE: mains frequency of %.4f Hz too low

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L530

Mains frequency below 49.9 Hz. Can be set here https://github.com/mcjurij/mfm/blob/f84038333c263206020d84a0b6d88ab9bdeef04e/embedded/Pico_W/mfm/conf.h#L15 .

#### NOTE: mains frequency of %.4f Hz too high

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L544

Mains frequency above 50.1 Hz. Can be set here https://github.com/mcjurij/mfm/blob/f84038333c263206020d84a0b6d88ab9bdeef04e/embedded/Pico_W/mfm/conf.h#L16 .

#### ERROR: rise vs fall deviation of %.4f%% too large

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L559

Deviation of the measurements for falling and rising edges is too large.

#### WARNING: rise vs fall deviation of %.4f%% large

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L573

Deviation of the measurements for falling and rising edges is large.

#### WARNING: %.4f is too big of a jump

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L588

Jump between two consecutive measurements is ​​too large.

#### ERROR: rise diff stddev of %.2f is too high

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L603

The standard deviation of the measurement with rising edges is too large.

#### ERROR: fall diff stddev of %.2f is too high

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L618

The standard deviation of the measurement with falling edges is too large.

#### ERROR: %d erratic diff(s)

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L634

Erratic differences (called diffs in the code). Differences are the time between two either rising or falling edges.

#### WARNING: %d corrected diffs

https://github.com/mcjurij/mfm/blob/bba71c24176cf02726d72ed47665e61a6f7a76e6/embedded/Pico_W/mfm/freq.c#L650

More than 4 corrected differences. Differences must always be corrected if a time stamp was determined on the ATmega side that had not yet noticed the last overflow of the counter register.

#### Display incidents in the mfm_bwatcher

For a file with measurement values, for example `meas_data_<Pico ID>__<Date>.txt`, you can also load the matching `incidents_<Pico ID>_<Date>.txt`. With the same Pico ID and the same date. It looks like this:
![Binge Watcher Incidents zoom](photos/bwatcher_incid_zoom.png "Binge Watcher Incidents zoom")

In order to be able read each incident you have to zoom in a lot along the x-axis until they no longer overlap. Therefore the x-axis is selected and displayed in blue. Zoom with mouse wheel.
This is a typical case of incidents that are triggered by a ripple control signal. That's probably 95% of all incidents.

Showing incidents consumes a lot of CPU in mfm_bwatcher if you also go into follow mode. The widget used (QCustomPlot) is not optimized for this application.
