# mfm
Mains Frequency Measurement



## Über die zwei grundsätzlichen Arten Frequenzen zu messen


1. Man kann ein Gate für eine exakte Zeit (zB genau 1 Sekunde) öffenen und dann zählen wieviele Impulse, erkannt an einer Triggerschwelle, in dieser Zeit erkannt wurden. Dies ist das "klassische" Frequency Counting. Bei hohen Frequenzen funktioniert das ausgezeichnet.

2. Man kann warten bis der Trigger erreicht ist, lässt dann immer wieder neu triggern und bestimmt den Zeitunterschied zwischen den beiden Triggerevents. Um eine hohe Genauigkeit zu erreichen wird dies über eine längere Zeit gemacht, d.h. über mehrer Perioden des Eingangssignals.

Für eine genaue Bestimmung einer Frequenz im Bereich von 50 Hz, also einer sehr niedrigen Frequenz, mit einer Genauigkeit von mindestens +/ 1mHz eher besser, ist Verfahren Nr 2 offensichtlich das Einzig geeignete. Bei Verfahren Nr 1 müsste man sehr lange Messen um genug Impulse gesehen zu haben.

Verfahren 2 hat allerdings einen Nachteil. Will man immer die selbe Anzahl an Perioden messen, was hier gemacht wird denn es werden immer 50 Perioden abgemessen, dann ist die MessDAUER abhängig vom MessWERT. Bei einer Frequenz über 50 Hz ist die Messung kürzer als 1 Sekunde. Bei einer Messung unter 50 Hz, länger. Dies zu erwähnen ist wichtig, denn viele mathematische Verfahren gehen von äquidistanten Werten aus. Und auch die Berechnung der Netzzeit benötigt Messwerte im Abstand von 1 Sekunde.

Die Platinen mit der Schaltung zur Netzfrequenzmessung werde ich ab jetzt Counter 1 & 2 nennen. Der Name Counter rührt daher, dass die Hauptaufgabe bei der Frequenzmessung das Zählen zwischen 2 Triggerevents ist. Die übernimmt der ATmega als erste Stufe, hier werden erstmal nur Zeitstempel bestimmt. In der zweiten Stufe wird aus den Zeitstempeln des ATmega vom Pico W eine Frequenz berechnet.


## Über Quarze

Es gibt verschiedene Arten eine Frequenz von zB exakt 10 Mhz zu erzeugen. Standardquarze, temperaturkompensierte Quarze und Quarzöfen (Oven Controlled Crystal Oscillator, OCXO) sind die gängisten Methoden, wobei jeweils eine Balance zwischen Kosten/Aufwand und Anforderung der Anwendung gefunden werden muss. Ein Standardquarz war für meine Geschmack zu ungenau. Standardquarze sind recht temperaturabhängig. Diese Temperaturabhängigkeit konnte ich in meinen Experimenten deutlich sehen. Eine Genauigkeit von +/- 1 mHz war so nicht zu erreichen, IMO.

Ein temperaturkompensierter Quarz (Temperature Compensated Crystal Oscillator, TCXO) korrigiert den Fehler nach Messung der Temperatur des Quarzes. Das Verfahren ist i.A. sehr viel genauer als ein Standardquarz. Diese sind aber schwer zu bekommen.

Ein Quarzofen hält die Temperatur des Quarzes auf einem konstantem Level. Die Genauigkeit der Frequenz hängt hier im wesentlichem nur noch von der Genauigkeit der Temperaturmessung und des Regelkreises ab. Man bekommt zB den von mir verwendeten Quarzofen für unter 20 Eur auf EBay aus China, inkl. Versand. Und für einen OCXO ist das sehr sehr günstig.  Und ist der Grund warum ich mir die Option TCXO nicht weiter angeschaut habe.


## Hardware

Das MFM Projekt (kurz MFM) besteht aus 4 hardware Komponenten. Wovon eine doppelt vorhanden ist.

* Ein GPS disciplined oscillator (GPSDO oder auch GPSDXO) mit 1PPS Ausgang

* Ein oven-controlled crystal oscillator (OCXO) mit 10 Mhz TTL Ausgang (Clock), versorgt über Steckernetzteil.

* Eine "Distributor" genannte Platine zum verdoppeln der 10Mhz Clock und 1PPS Signale, versorgt über Netzanschluss. Schaltplan: Distributor.

* Zwei Platinen mit der Schaltung der Netzfrequenzmessung. Schaltpläne: Counter ATmega328p v1 & v2. Beide sind fast identisch und haben 3 Eingänge:
  - Netzanschluss, zur Messung und Stromversorgung
  - 10 Mhz TTL Clock für ATmega328 CPU
  - 1PPS

Der Distributor hat 2 Ein- und 4 Ausgänge. 10 Mhz TTL auf 2x 10 Mhz TTL für die ATmegas. 1PPS auf 2x 1PPS.
Der OCXO hat neben TTL Ausgang auch einen mit Sinus. Dieser wird auch nach aussen geführt damit man mit einem Frequenzmesser aka Frequency Counter nachprüfen kann ob der QCXO auch genau 10 Mhz Ausgibt. Dabei kann der GPSDO als Referenz benutzt werden.


### GPSDO

### OCXO & Distributor

Mit OCXO meine ich die Platine links im Bild.  Der eigentliche Quarzofen, die Metallkiste (siehe Photo), ist von CTI und gebraucht. Nur die Platine ist neu. Es gibt natürlich bessere und genauere Quarzöfen und Beschaltungen aber für diese Anwendung ist die Genauigkeit mehr als ausreichend. Im EEVblog Forum hat wohl der ein oder andere Probleme mit der Einstellung über das Poti, da das Poti am Anschlag ist aber die Frequenz von 10 Mhz nicht genau erreicht wird. Ich hatte das Problem nicht. 

Der Distributor ist rechts im Bild und einfach nur eine Schaltung mit Logikgattern die die entrspr. Signale verstärken.
![Distributor](hardware/distributor/distributor.png "Distributor")

Die Box hat einen Eingang für 1PPS und 5 Ausgänge:


### Counter 1 & 2

Die Counter sind bis auf zwei Unterschiede identisch aufgebaut. Beide haben ein Front- und ein Backend. Die Trennung ist dort, wo die Optokoppler sind. Die Masse der +/-5 V Versorgungspannungen ist im Frontend an die Netzspannung gekoppelt. Durch die Optokoppler wird die Netzspannung abgetrennt. Deswegen gibt es zwei Massen GND (Frontend) und GND_2 (Backend) und zwei Spannungsversorgungen.

Die Counter müssen nicht zwingend an 230 V angeschlossen werden. Es geht auch mit einem Steckernetzteil was eine Wechselspannung von c.a. 8 - 10 V liefern sollte. Diese muss an D1 (Suppressordiode, auch TVS Diode) angelegt werden. R1, der Varistor RVAR1 und die Sicherung F1 entfallen dann. Dann braucht man allerdings noch eine Lösung für die symetrische +/-5V Versorgung des Frontends. Insg. daher die aufwändigere Lösung --allerdings Pflichtprogramm für jeden ohne Trenntrafo. Insb. dann wenn mit einem Oszi auf Front Ende Seite gemessen werden soll. 

Der Signalweg anhand Counter 1, Schaltplan counter_mcu_v2.
![Counter ATmega328p v2](hardware/counter_atmega328p_v2/counter_atmega328p_v2.png "Counter ATmega328p v2")


Frontend:
Über den Spannungsteiler R1,R2 und der Suppressordiode D1 wird die Eingangsspannung auf einen Bruchteil verringert und abgeschnitten. Über RV2 dann weiter verringert und an Komparator U1 gegeben. Dieser vergleicht den Sinus mit einem festem Wert der über RV1 kommt. Das ergibt am Ausgang des Komparators immer einen Wechsel genau dann wenn die Netzspannung über oder unter 0V geht. Dieses Rechtecksignal wird nun durch U3A entkoppelt und an 2 Optokoppler übergeben, die Front- und Backend trennen. 

Backend:
Die Signale aus den 2 Optokopplern werden durch ein S-R Latch U4B, U4A, zu einem Rechtecksingal vereint. Der ATmega hat nur einen Input Capture Eingang (Pin 14). Dieser Eingang wird benutzt um die Zeit zu stoppen in der das Rechteck 0 und 1 ist. Die Genauigkeit ergibt sich dabei aus der Clock des ATmegas, die über den Distributor vom OCXO kommt. Die Zeitstempel werden über UART an den Pico W geschickt. Beide MCUs werden über U7E, U7F und U4D mit dem 1PPS Signal versorgt. Warum das so ist beschreibe ich unter Software.

Hinweis: die Anschlüsse zur Programmierung des ATmegas sind nicht im Schaltplan eingezeichnet.

Hinweis: Das Netzteil für das Frontend ist in einem eigenem Schematic.
![symmetric power supply](hardware/counter_ps/counter_ps.png "Counter PS")


#### Atmel ATmega328p

Der ATmega328p ist ein sehr bekannter Vertreter aus der Atmel AVR Familie. https://www.microchip.com/en-us/product/ATmega328
(Microchip hatte Atmel 2016 gekauft).

Ich benutze ihn hauptsächlich weil ich ihn vorher schon kannte, und er das Input Capture Feature hat (Pin 14 / PB0).  

#### Raspberry Pico W

Der Raspberry Pico W (eigentlich Raspberry Pi Pico W) ist recht neu. Er basiert auf der von Raspberry Pi entwickelten RP2040 MCU, ein dual ARM Cortex M0+ Chip. Takt 133 Mhz.
https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html#raspberry-pi-pico-w

Ich benutze ihn hauptsächlich weil er Wlan, eine gut dokumentierte API und einen geringen Preis hat.

Zwar ist die Spannungsversorung auf 5V ausgelegt, sie wird aber vom Pico W auf 3,3V umgewandelt. Alle I/O Pins dürfen somit nur mit max. 3,3V betrieben werden. 3,3V für eigene Bauteile können beim Pico W abgegriffen werden (Pin 36/ 3V3). Das ist nützlich für das level shifting 3,3 V <-> 5 V.


## Software

Die Software besteht aus 4 Komponenten. Eine Embedded SW für jeweils ATmega und Pico W. Einem Server der die Daten die per WLan reinkommen entgegennimmt. Und einem einfachen Tool zur graphischen Ausgabe der Daten.

### Embedded

Um die krosskompilierte SW auf die MCUs zu übertragen werden 2 völlig verschiedene Methoden benutzt. Der ATmega wird über einen In System Programmer (ISP) programmiert. Der Pico W hat einen Taster. Wird der beim Einschalten des Pico Ws gehalten so geht der Pico W in eine spezielle Boot-Sequenz (die nicht überschrieben werden kann). Damit kann man über USB einen "Massenspeicher" mounten und eine .uf2 Datei dort hinkopieren. Das erkennt der Pico W und überträgt diese dann in sein Flashspeicher.

Ich kann mir vorstellen, dass der Pico W zur Programmierung des Atmegas benutzt werden kann. Das habe ich aus Zeitmangel nicht verfolgt. Es hätte nicht nur den Vorteil, dass man keinen extra ISP braucht sondern, dann man diesen nicht anschliessen muss, was etwas fummellig ist. Allerdings braucht man wegen der 3,3V I/O Spannung des Picos einen Treiber (auch level shifter oder logic converter genannt) zur Umsetzung und da sehe ich Platzprobleme. Die man allerdings nicht hätte wenn man einen kleineren Vertreter der ATmel Serie nehmen würde (ATtiny).  Der ATmega328p ist für diese Anwendung klar überdimensioniert.



#### Atmel ATmega328p

Mein ISP https://guloshop.de/shop/Mikrocontroller-Programmierung/guloprog-der-Programmer-von-guloshop-de::70.html


#### Raspberry Pico W


SDK einrichten. https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html#sdk-setup


### Auf Linux PC
