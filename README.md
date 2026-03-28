# pwrinspace_zadanie_1

Projekt składa się z 2 modułów dla dwóch układów ESP32. Dlaczego są dwa? Uznałem, że będzie to doskonała praktyka w pracy z magistralą CAN przed drugim zadaniem, a ponieważ realizowałem to zadanie na rzeczywistym sprzęcie, nie będę musiał przebudowywać stanowiska testowego.

Schemat połączeń modułów:

```mermaid
graph LR
    ESP1[ESP32 Node 1 Receiver node1_rx]
    ESP2[ESP32 Node 2 Transmitter node2_tx]
    CAN1[CAN Transceiver 1]
    CAN2[CAN Transceiver 2]
    SD[MicroSD Card Reader]

    ESP1 -- "GPIO 26 <-> TX" --- CAN1
    ESP1 -- "GPIO 25 <-> RX" --- CAN1

    ESP1 -- "GPIO 23 <-> MOSI" --- SD
    ESP1 -- "GPIO 19 <-> MISO" --- SD
    ESP1 -- "GPIO 18 <-> CLK" --- SD
    ESP1 -- "GPIO 5 <-> CS" --- SD

    ESP2 -- "GPIO 26 <-> TX" --- CAN2
    ESP2 -- "GPIO 25 <-> RX" --- CAN2

    CAN1 -- "CAN_H <-> CAN_H" --- CAN2
    CAN1 -- "CAN_L <-> CAN_L" --- CAN2
```
Tak to wyglądało na żywo :)

![20260328_130406](https://github.com/user-attachments/assets/bcd1df77-4219-4eb0-83fa-7d48f196384c)


## node1_rx
### can_receive_task
Nasłuchuje magistralę CAN i czeka na nadejście danych. Składa je z 3 wiadomości w jedną i przesyła do kolejki. Jestem zmuszony korzystać z 3 wiadomości ze względu na ograniczenie protokołu CAN, w którym ładunek użyteczny nie może przekraczać 8 bajtów.

### sd_write_task
Otwiera plik do zapisu danych. Czeka na dane z kolejki z zadania can_receive_task. Zapisuje dane w pamięci RAM i wymusza ich synchronizację z kartą SD co 100 pakietów, aby uniknąć zawieszeń magistrali przy wolnym zapisie.

## node2_tx
Wykonuje dwa główne zadania:

Prosta symulacja lotu rakiety.
Przygotowanie i wysyłka wiadomości przy użyciu interfejsu CAN.

### Symulacja lotu
Składa się z 5 stanów: **STATE_IDLE**, **STATE_BOOST**, **STATE_COAST**, **STATE_DESCENT**, **STATE_LANDED**.
W zależności od tego, w jakim stanie znajduje się rakieta, wysyłane są różne dane, takie jak: _timestamp_, _packet_id_, _chamber_pressure_, _tank_pressure_, _accel_z_, _altitude_.
Ogólnie symulacja lotu wygląda następująco:

<img width="1200" height="243" alt="image" src="https://github.com/user-attachments/assets/81495942-7bec-4bd7-9851-9adaf259e5e3" />


<img width="1000" height="1200" alt="wykres" src="https://github.com/user-attachments/assets/5b6f5c0d-2d54-44dc-bffa-b3666ff2bd0f" />


Dodatkowo w stanach STATE_IDLE i STATE_LANDED częstotliwość próbkowania telemetrii wynosi 1 Hz, co ma na celu zmniejszenie obciążenia magistrali. W stanach BOOST, COAST, DESCENT częstotliwość wzrasta do 50 Hz.

### Wysyłka wiadomości
Ze względu na ograniczenia protokołu CAN, w którym nie możemy wysłać więcej niż 8 bajtów ładunku użytecznego, musiałem podzielić wiadomości na 3 części, z których każda zawiera po 2 pola. Każda wiadomość ma swój identyfikator (ID), dzięki któremu odbiornik wie, co się w niej znajduje.

## Odpowiedź na pytanie: 
**Q**: Zastanów się, jak to obejść, by nie gubić danych – szczególnie jeśli zwiększymy częstotliwość próbkowania do chociażby 1000 Hz.<br>
**A**: W takim przypadku moglibyśmy wykorzystać 2 bufory: Bufor A zapisuje dane, a gdy tylko się zapełni, dane zaczyna zapisywać Bufor B. W tym czasie Bufor A przesyła dane na kartę SD. Krótko mówiąc, stosujemy mechanizm Ping-Pong Buffer.



