
## Software

Die Software besteht aus 4 Komponenten. Eine Embedded SW für jeweils ATmega und Pico W. Einem Server, der die Daten die per WLan reinkommen, entgegennimmt. Und einem einfachen Tool zur graphischen Ausgabe der Daten.

### Embedded

Um die krosskompilierte SW auf die MCUs zu übertragen werden 2 völlig verschiedene Methoden benutzt. Der ATmega wird über einen In System Programmer (ISP) programmiert. Der Pico W hat einen Taster. Wird der beim Einschalten des Pico Ws gehalten, so geht der Pico W in eine spezielle Boot-Sequenz (die nicht überschrieben werden kann). Damit kann man über USB einen "Massenspeicher" mounten und eine .uf2 Datei dort hinkopieren. Das erkennt der Pico W und überträgt diese dann in sein Flashspeicher.

Ich kann mir vorstellen, dass der Pico W zur Programmierung des Atmegas benutzt werden kann. Das habe ich aus Zeitmangel nicht verfolgt. Es hätte den Vorteil, dass man keinen extra ISP braucht. Allerdings braucht man wegen der 3,3V I/O Spannung des Picos einen Treiber (auch level shifter oder logic converter genannt) zur Umsetzung und da sehe ich Platzprobleme. Die man allerdings vermutlich nicht hätte, wenn man einen kleineren Vertreter der ATmel Serie nehmen würde (ATtiny).  Der ATmega328p ist für diese Anwendung klar überdimensioniert. Zwar laufen ATmega/ATtiny auch mit 3,3V, ich weiss aber nicht ob das für 10Mhz reicht. Und an den Eingängen des ATmega/ATtiny dürfte dann nicht mehr als 3,3 + 0,5 V angelegt werden. 



#### Atmel ATmega328p

Der Source für den ATmega ist hier:
![ATmega 328 src](embedded/ATmega328 "ATmega 328 src")

Es gibt nur eine Datei: `main.c`. Da der ATmega nur die Zeitstempel zwischen den Flanken des Rechtecksignals bestimmt und diese per UART an den Pico W schickt, passiert hier nicht viel. Das 1PPS Signal wird benutzt um den Zähler für die Zeitstempel auf 0 zu setzen.

Damit das Makefile durchläuft muss die avr-gcc Toolchain und zum flashen der avrdude installiert sein. Wenn der ISP zu dem im Makefile passt und angeschlossen ist, kann mit `make program` in einem Schritt kompiliert und geflashed werden.


#### Raspberry Pico W


Der Source für den Pico W ist hier:
![Pico W src](embedded/Pico_W/mfm "Pico W src")

Ich benutze kein OS. Der Programmierer legt fest, was auf den 2 Kernen des Pico Ws, Core0 und Core1, laufen soll. Ich habe es so aufgeteilt, dass die Annahme (über UART) und Verarbeitung der Zeitstempel, die vom ATmega kommen, auf Core1 passiert. Die Kommunikation mit dem Server auf Core0 (dort wird main() gestartet).

Der Wlan Chip benutzt Interrupts, die mit Core0 verdrahtet sind und daher kann der Wlan Code nur auf Core0 laufen.

Um eine genaue Zeit zur ermitteln, holt sich der Pico W periodisch die Zeit von mfm_server. Dieser gibt die Zeit des Hostrechners zurück, die ungenau ist, was kein Problem ist solange sie genau genug ist bezüglich um die richtige Sekunde zu bestimmen (der Hostrechner bekommt seine Zeit wiederum i.d.R. über NTP. Das Verfahren ist abhängig von der Netzwerklatenz). Diese wird nun mit dem 1PPS Signal abgeglichen und damit erhält man eine sehr genaue Zeit. 
Ich benutze nicht die Uhr (RTC) des Picos. Der Standardquarz des Picos macht eine ständige Korrektur der Zeit nötig, egal wie man es macht.  Der Pico hat einen Zähler der ab Start hochzählt. Zu diesem addiere ich die Mikrosekunden seit 1.1.1970 0 Uhr.  In einer Interruptroutine, die an das 1PPS Signal gekoppelt ist, wird jede Sekunde geschaut um wieviele Mikrosekunden der Zähler (+ Mikrosekunden seit 1.1.1970 0 Uhr) korrigiert werden muss.
Da die Zeitstempel des ATmegas ebenfalls mit dem 1PPS Signal synchronisiert sind, kann die Messzeit (zumindest in der Theorie) sehr genau bestimmt werden. Sobald eine Messung stattgefunden hat kommt vom Pico nur die Information in welcher Sekunde die Messung war und vom ATmega die Mikrosekunde (Start/Ende der Messung).  Dabei ist es wichtig zu beachten, dass der Start oder das Ende der Messung nicht auf eine Sekundengrenze liegen müssen und dass eine Messung länger (oder kürzer) als eine Sekunde sein kann.

- `conf.h` - Konfiguration wie MAINS_FREQ
- `freq.c` - Empfängt Zeitstempel vom ATmega und berechnet daraus Frequenz. Läuft auf Core1.
- `main.c` - enthält main() was auf Core0 startet, startet Core1. Empfängt auf Core0 die Daten die von `freq.c` in den `ringbuffer.c` geschrieben wurden und sendet sie an den mfm_server.
- `ntime.c` - Bestimmung der genauen Zeit aus Kombination mit Zeit vom Linux PC und 1PPS.
- `proto.c` - Protokoll-definition.
- `ringbuffer.c` - Speichern von Messwerten wenn Netzwerkverbindung (kurz) nicht verfügbar.


Zunächst mal das C SDK einrichten. https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html#sdk-setup
Unter https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html#quick-start-your-own-project steht welche Linux Packages installiert werden müssen.

Git clone und Umgebungsvariable setzen.
```
git clone https://github.com/raspberrypi/pico-sdk.git

cd pico-sdk
git submodule update --init

export PICO_SDK_PATH=$HOME/<path to pico sdk>/pico-sdk/
```

In das Pico W Verzeichnis wechseln, cmake Aufruf.

```
cd embedded/Pico_W/

mkdir build
cd build

cmake -DPICO_BOARD=pico_w -DWIFI_SSID="<your SSID>" -DWIFI_PASSWORD="<your password>" ..
```

Das sollte zu diesen Ausgaben führen. Ich habe meine Pfade hier geschwärzt.
![cmake Ausgabe](photos/cmake_run.png "cmake")

Zum Bauen `make -j9`.  Die UF2 Datei zum flashen ist unter `embedded/Pico_W/build/mfm/mfm_pico_w.uf2`  (747 kb).



### Auf Linux PC

Auf einem Linux PC laufen der mfm_server, der die Daten die über Wlan von den Pico Ws kommen annimmt, weiterverarbeitet und in Dateien schreibt. Diese können von einer GUI, dem "Binge Watcher" bzw mfm_bwatcher, gelesen werden. 

Die Ausgaben, die mit printf() gemacht werden, werden über USB übertragen. Dazu auf Linux PC `minicom -C minicom.log -b 115200 -o -D /dev/ttyACM0` eingeben, nachdem der Pico W gestartet wurde. Die Übertragung der Ausgaben über USB hängt sich bei mir manchmal auf (nach mehrere Stunden oder Tagen). Der Pico W läuft dann aber noch.


#### mfm_server

Der mfm_server sollte mit einem `make` direkt bauen.
Der Server startet pro Verbindung, also pro Counter, einen Thread. Die zwei Threads übergeben dann die Messwerte an einen Thread, der die Daten pro Counter rausschreibt und zusätzlich kombiniert. 

Der mfm_server ist (noch) kein richtiger Server, es fehlt das sog daemonizing, dazu gehört das Entkoppeln vom Terminal. Z.Z. gibt es auch keine Möglichkeit den Server mit einem Kontrollprogramm sauber runterzufahren.

- `conn_slots.c` - jeder Pico W bekommt einen eigenen connection slot, nutzt die Pico ID um zu erkennen, dass der selbe Pico W sich erneut angemeldet hat.
- `file_mgr.c` - File Management und Rotation.
- `process_data.c` - Datenauswertung.
- `proto.c` - Protokoll-definition, exakte Kopie der Datei im embedded/Pico_W/mfm Verzeichnis.
- `server.c`- Enhält main(), nimmt Netzwerkverbingen an und startet pro Pico W einen Thread.

Die Picos haben eine eindeutige ID, diese kann mit https://github.com/mcjurij/mfm/blob/85a41c4bb33529b7771d14aba3f1979061e204ab/embedded/Pico_W/mfm/main.c#L51 abgefragt werden. Im mfm_server dient sie insb. dazu die Dateinamen für die Counter-bezogenen Dateien zu bestimmen. Nachdem ein Pico W eine Verbindung zum mfm_server aufgebaut hat sendet er als erstes seine ID. An einem Beispieltag, ich nehme den 2023-09-25, wird das schnell klar. Mit us-Epoch sind die Mikrosekunden seit dem 1.1.1970 0 Uhr gemeint. Meine Pico Ws haben die IDs Counter 1: E661A4D41723262A, Counter 2: E661A4D41770802F.


| Dateiname                                | Inhalt        |
| ---------------------------------------- | ------------- |
|`meas_data_E661A4D41723262A_2023-09-25.txt`|Messwerte von Counter 1 mit us-Epoch Zeit|
|`meas_data_E661A4D41770802F_2023-09-25.txt`|Messwerte von Counter 2 mit us-Epoch Zeit|
|`meas_data_local_E661A4D41723262A_2023-09-25.txt`|Messwerte von Counter 1 mit lokaler Zeit|
|`meas_data_local_E661A4D41770802F_2023-09-25.txt`|Messwerte von Counter 2 mit lokaler Zeit|
|`meas_sgfit_E661A4D41723262A_2023-09-25.txt`|Messwerte von Counter 1 mit us-Epoch Zeit, mit Interpolation & Savitzky-Golay Filter|
|`meas_sgfit_E661A4D41770802F_2023-09-25.txt`|Messwerte von Counter 2 mit us-Epoch Zeit, mit Interpolation & Savitzky-Golay Filter|
|`meas_sgfit_local_E661A4D41723262A_2023-09-25.txt`|Messwerte von Counter 1 mit lokaler Zeit, mit Interpolation & Savitzky-Golay Filter|
|`meas_sgfit_local_E661A4D41770802F_2023-09-25.txt`|Messwerte von Counter 1 mit lokaler Zeit, mit Interpolation & Savitzky-Golay Filter|
|`meas_merge_2023-09-25.txt`|Verschmolzene Messwerte von Counter 1 & 2 mit us-Epoch Zeit, mit Interpolation|
|`meas_merge_sgfit_2023-09-25.txt`|Verschmolzene Messwerte von Counter 1 & 2 mit us-Epoch Zeit, mit Interpolation & Savitzky-Golay Filter|
|`gridtime_2023-09-25.txt`|Netzzeit mit us-Epoch Zeit|
|`gridtime_local_2023-09-25.txt`|Netzzeit mit lokaler Zeit|
|`incidents_E661A4D41723262A_2023-09-25.txt`|Incidents von Counter 1|
|`incidents_E661A4D41770802F_2023-09-25.txt`|Incidents von Counter 2|

Die Interpolation ist einfach zwischen zwei Messwerten linear interpoliert, und zwar so, dass man einen Wert pro Sekunde erhält. Der Savitzky-Golay Filter kann (in dieser Form) nur mit äquidistanten Werten arbeiten. Bei `meas_merge_*` wird der Mittelwert aus den beiden Interpolationen von Counter 1 und 2 genommen. Bei `meas_merge_sgfit_*` wird für Counter 1 & 2 jeweils erst interpoliert, dann jeweils der Savitzky-Golay Filter angewendet und danach der Mittelwert von diesen beiden Werten genommen.

Die Netzzeit wird aus den Messwerten berechnet die in `meas_merge_sgfit_*` geschrieben werden.

Welchen Effekt der Savitzky-Golay Filter auf die Messwerte hat, kann man sehr schön im mfm_bwatcher sehen, in dem man sich zB. `meas_data_E661A4D41723262A_2023-09-25.txt` _und_ `meas_sgfit_E661A4D41723262A_2023-09-25.txt` anschaut. Implementation siehe  https://github.com/mcjurij/mfm/blob/5b119eb627beb55587a3f4324e777724062568a9/mfm_server/process_data.c#L553 


#### mfm_bwatcher

Ein einfaches Tool zur graphischen Ausgabe der Daten, ähnlich einem Funktionsplotter, aber mit ein paar speziellen Features für diese Anwendung. Es muss Qt 6 installiert sein. Aktuell benutze ich Qt 6.4.3.
Zum Konfigurieren des Projekts auf File->Open File or Project gehen, dann `mfm_bwatcher.pro` laden, dann bei "Configure Project" nur "Desktop ..." auswählen, und auf "Configure Project" Button klicken. Dann unten links auf das grüne Dreieck "Run" clicken. Damit wird gebaut und die mfm_bwatcher GUI gestartet.


![Binge Watcher Follow Mode](photos/bwatcher_1.png "Binge Watcher Follow Mode")
Dieser Screenshot zeigt die Daten von Counter 1: `meas_data_E661A4D41723262A_2023-09-27.txt` und `meas_sgfit_E661A4D41723262A_2023-09-27.txt`. Man sieht den Effekt des Savitzky-Golay Filters.
Im "Follow mode" werden die Dateien vom mfm_server immer am Ende neu gelesen und der Graph im Sekundentakt erneuert. Zu erkennen ist der Follow mode an dem Kästchen mit Wert und Pfeil an der rechten y-Achse (im Source AxisTag genannt).

![Binge Watcher Follow Mode](photos/bwatcher_2.png "Binge Watcher Follow Mode")
Dieser Screenshot zeigt die Daten von Counter 1: `meas_sgfit_E661A4D41723262A_2023-09-27.txt` und Counter 2: `meas_sgfit_E661A4D41770802F_2023-09-27.txt`. Die beiden Kurven liegen sehr genau übereinander, da die Savitzky-Golay Filter die Unterschiede fast komplett weg-smoothen.

Die Zeit auf der x-Achse ist immer die lokale Zeit.
