# mfm
Mains Frequency Measurement

English: [English version](README_en.md)

Ziel des Projektes ist eine möglichst genaue Messung der Netzfrequenz.

* Dies ist ein reines Spassprojekt,
* ich verdiene mein Geld weder mit Hardwareentwicklung noch mit Embeddedprogrammierung,
* einige meiner Designentscheidungen sind daher u.U. suboptimal. :-)

Mit "Linux PC" ist in meinem Fall ein Fedora (z.Z. Fedora 37) gemeint. Die genutzte Distro spielt aber keine Rolle.


## Über die zwei grundsätzlichen Arten Frequenzen zu messen

1. Man kann ein Gate für eine exakte Zeit (zB genau 1 Sekunde) öffenen und dann zählen wieviele Impulse, erkannt an einer Triggerschwelle, in dieser Zeit erkannt wurden. Dies ist das "klassische" Frequency Counting. Bei hohen Frequenzen funktioniert das ausgezeichnet.

2. Man kann warten bis der Trigger erreicht ist, lässt dann immer wieder neu triggern und bestimmt den Zeitunterschied zwischen den beiden Triggerevents. Um eine hohe Genauigkeit zu erreichen wird dies über eine längere Zeit gemacht, d.h. über mehrer Perioden des Eingangssignals.

Für eine genaue Bestimmung einer Frequenz im Bereich von 50 Hz, also einer sehr niedrigen Frequenz, mit einer Genauigkeit von mindestens +/ 1mHz eher besser, ist Verfahren Nr 2 offensichtlich das einzig geeignete. Bei Verfahren Nr 1 müsste man sehr lange Messen um genug Impulse gesehen zu haben.

Verfahren 2 hat allerdings einen Nachteil. Will man immer die selbe Anzahl an Perioden messen, was hier gemacht wird denn es werden immer 50 Perioden abgemessen, dann ist die MessDAUER abhängig vom MessWERT. Bei einer Frequenz über 50 Hz ist die Messung kürzer als 1 Sekunde. Bei einer Messung unter 50 Hz, länger. Dies zu erwähnen ist wichtig, denn viele mathematische Verfahren gehen von äquidistanten Werten aus. Und auch die Berechnung der Netzzeit benötigt Messwerte im Abstand von 1 Sekunde.


## Über Quarze

Es gibt verschiedene Arten eine Frequenz von zB exakt 10 Mhz zu erzeugen. Standardquarze, temperaturkompensierte Quarze und Quarzöfen (Oven Controlled Crystal Oscillator, OCXO) sind die gängisten Methoden, wobei jeweils eine Balance zwischen Kosten/Aufwand und Anforderung der Anwendung gefunden werden muss. Ein Standardquarz war für meinen Geschmack zu ungenau. Standardquarze sind recht temperaturabhängig. Diese Temperaturabhängigkeit konnte ich in meinen Experimenten deutlich sehen. Eine Genauigkeit von +/- 1 mHz war so nicht zu erreichen, IMO.

Ein temperaturkompensierter Quarz (Temperature Compensated Crystal Oscillator, TCXO) korrigiert den Fehler nach Messung der Temperatur des Quarzes. Das Verfahren ist i.A. sehr viel genauer als ein Standardquarz. Diese sind aber schwer zu bekommen.

Ein Quarzofen hält die Temperatur des Quarzes auf einem konstantem Level. Die Genauigkeit der Frequenz hängt hier im wesentlichem nur noch von der Genauigkeit der Temperaturmessung und des Regelkreises ab. Man bekommt zB den von mir verwendeten Quarzofen für unter 20 Eur auf EBay aus China, inkl. Versand. Und für einen OCXO ist das sehr sehr günstig.  Und ist der Grund warum ich mir die Option TCXO nicht weiter angeschaut habe.


[Beschreibung Hardware](hardware.md)

[Beschreibung Software](software.md)
