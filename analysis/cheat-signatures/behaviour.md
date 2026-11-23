# Serverseitige Verhaltenssignaturen

Stand: 2026-11-23. Ergänzt `signatures.toml` dort, wo clientseitige Merkmale fehlen oder umgangen
werden können (siehe `inventory.md`, Abschnitt Restlücken). Alles auf hoher Ebene; die Schwellwerte
sind **Startwerte** [vermutet] und müssen mit Daten ehrlicher Spieler kalibriert werden
(zuerst nur `log`, nach 2–4 Wochen Verteilung ansehen, dann `flag`).

## Grundlagen

- **Zeitstempel:** Ankunftszeit jedes relevanten Pakets im Map-Server in Millisekunden
  (`gettick()`), pro Charakter in einem Ringpuffer.
- **Latenzkorrektur:** RTT und Jitter pro Verbindung aus dem OdinGuard-Heartbeat (gleitender Median
  der letzten 60 s). Reaktionszeit = Ankunft der Antwort − Versand des auslösenden Pakets − RTT/2.
- **Paketnamen** statt IDs (IDs hängen von `packetver` des 2018-06-20-Clients ab):
  `CZ_USE_ITEM`, `CZ_USE_SKILL`, `CZ_USE_SKILL_TOGROUND`, `CZ_REQUEST_ACT`, `CZ_REQUEST_MOVE`,
  Inventar/Lager/Wagen-Verschiebepakete, NPC-Dialogpakete; auslösend `ZC_PAR_CHANGE` (HP/SP),
  `ZC_MSG_STATE_CHANGE`/Status-Start.
- **Mindeststichprobe:** keine Entscheidung unter der angegebenen Ereigniszahl; Auswertung je
  Fenster (z. B. 30 min) und je Kontext (PvM/WoE/PvP getrennt).
- **Robuste Statistik:** Median und Interquartilsabstand (IQR) statt Mittelwert/Standardabweichung;
  Variationskoeffizient CV = Standardabweichung / Mittelwert nur nach Ausreißer-Kappung (5 %/95 %).
- **Kombination:** einzelne Signale erhöhen einen Score; `kick`/`ban` nur bei mehreren unabhängigen
  Signalen oder zusätzlicher Client-Signatur (z. B. `OG-AHK-*`, `OG-DD-*`).

## B1 – Autopot (Autopot Fresh, RoMedic, AHK-Varianten)

Muster [vermutet aus gelesener Konfiguration: Abfrageintervall ~100 ms + 10 ms, fester Prozent-Schwellwert]:
sehr schnelle und sehr gleichmäßige Item-Benutzung nach einem HP/SP-Abfall unter eine feste Schwelle.

| Signal | Messung | Startschwelle | Konfidenz | Aktion |
|---|---|---|---|---|
| B1.1 Reaktionszeit | Zeit von `ZC_PAR_CHANGE` (HP unter Schwelle) bis `CZ_USE_ITEM` (Heil-Item), RTT-korrigiert | Median < 120 ms **und** IQR < 40 ms bei ≥ 30 Ereignissen | medium | flag |
| B1.2 Reaktionszeit streng | wie B1.1 | Median < 80 ms **und** IQR < 25 ms bei ≥ 50 Ereignissen | medium-high | flag + Review, kick nur mit Client-Signal |
| B1.3 Feste Auslöseschwelle | HP-% beim Auslösen über alle Ereignisse | Standardabweichung < 2 Prozentpunkte bei ≥ 30 Ereignissen, obwohl die Schadenshöhe stark schwankt (CV Schaden > 0,3) | medium | flag |
| B1.4 Status-Heilung | Zeit von Status-Start (Gift, Fluch, Stille, Blindheit) bis Heil-Item/-Skill | Median < 150 ms, IQR < 40 ms bei ≥ 15 Ereignissen | medium | flag |
| B1.5 Heilen ohne Fokus-Pausen | Anteil der HP-Abfälle unter Schwelle, auf die *keine* Benutzung folgt | < 1 % über ≥ 200 Abfälle in Kämpfen > 30 min | low-medium | log |

Menschliche Referenz [vermutet, Literaturwerte]: einfache Reaktionszeit ~200–300 ms mit deutlich
größerer Streuung; Ermüdung erhöht Median und Streuung über die Zeit – ein über Stunden konstanter
Median ist zusätzlich verdächtig.

## B2 – Makros / Eingabe-Automatisierung (Class_DD, AHK-Spam, Inventar-Makros)

Muster: konstante Abstände zwischen gleichartigen Aktionen, oft in Blöcken; Treiber-Eingaben
(Class_DD) sind clientseitig nicht als synthetisch erkennbar, daher hier die Hauptlinie.

| Signal | Messung | Startschwelle | Konfidenz | Aktion |
|---|---|---|---|---|
| B2.1 Konstante Intervalle | Abstände aufeinanderfolgender gleichartiger Pakete (`CZ_USE_SKILL`, `CZ_REQUEST_ACT`, `CZ_USE_ITEM`) | CV < max(0,05; 2 × Jitter / Mittelwert) über ≥ 100 Ereignisse in einem Block | medium | flag |
| B2.2 Unmenschliche Rate | Ereignisse pro Sekunde je Typ | > 15/s über ≥ 10 s (gelesenes F1-Spam-Skript ≈ 25/s) | high | flag; kick bei Wiederholung |
| B2.3 Feste Paar-Sequenzen | Wiederkehrende Folge (z. B. Skill → Bodenziel) mit identischem Innenabstand | Innenabstand IQR < 5 ms über ≥ 50 Paare | medium | flag |
| B2.4 Inventar-Serien | Verschiebe-Pakete Inventar↔Lager/Wagen | ≥ 50 Verschiebungen mit CV < 0,05 im Abstand (gelesenes Skript: 2 × 150 ms) | medium | log → flag |
| B2.5 Ununterbrochene Dauer | Zeit ohne Pause > 2 s bei konstanter Aktionsfolge | > 60 min | medium | flag |

Hinweis: Netzwerk-Jitter verbreitert auch Makro-Intervalle; deshalb B2.1 jitterabhängig. Bei sehr
stabiler Verbindung (Jitter < 2 ms) sind Makro-Intervalle fast exakt periodisch [vermutet].

## B3 – Nodelay (nodelay.grf und ähnliche ACT-Kürzungen)

Muster: Der Client zeigt Angriffs-/Zauberanimationen verkürzt und schickt Folgeaktionen früher.
Der Server erzwingt seine eigenen Verzögerungen (Angriffs-/Skill-Delay, „can act“), daher äußert sich
Nodelay vor allem in **zu frühen Anfragen** [vermutet].

| Signal | Messung | Startschwelle | Konfidenz | Aktion |
|---|---|---|---|---|
| B3.1 Zu frühe Anfragen | Anteil `CZ_USE_SKILL`/`CZ_REQUEST_ACT`, die vor Ablauf der serverseitigen Sperre eintreffen und abgelehnt/verworfen werden | > 20 % bei ≥ 100 Anfragen (Referenz ehrlicher Clients erst messen) | medium | flag |
| B3.2 Rate über Animationslimit | Ausgeführte Angriffe/Skills pro Sekunde vs. aus ASPD/Skill-Daten berechnetes Maximum | > 105 % des Maximums über ≥ 30 s (deutet auf Server-Fehlkonfiguration **oder** Paketmanipulation, siehe B4) | high | flag |
| B3.3 Abbruch-Muster | Bewegungs- oder Item-Paket unmittelbar (< 50 ms) nach jeder Skill-Ausführung, über lange Serien | ≥ 80 % von ≥ 100 Skills | low | log |

Die eindeutigere Erkennung ist clientseitig (GRF-Hash, Frameanzahl der geladenen Body-`.act`).

## B4 – Paket-Werkzeuge (Paket-Editoren, Proxies, Bots)

Muster: Pakete, die der Original-Client so nicht erzeugt.

| Signal | Messung | Startschwelle | Konfidenz | Aktion |
|---|---|---|---|---|
| B4.1 Protokollfehler | Unbekannte Paket-ID für die `packetver`, falsche Länge, Felder außerhalb des Wertebereichs | 1 Ereignis | high | kick (Paket verwerfen), ban bei Wiederholung |
| B4.2 Identische Wiederholungen | Byte-identische Pakete (ID + Nutzdaten) in Folge, wo Spielzustand Variation erzwingt | ≥ 10 identische in < 1 s über der Server-Sperre | high | flag/kick |
| B4.3 Unmögliche Sequenzen | Item-Benutzung ohne Item im Inventar, NPC-Antwort ohne offenen Dialog, Lager-/Handelsaktion ohne offenes Fenster, Skill ohne gelernten Skill, Ziel außerhalb der Sicht | ≥ 3 in 10 min (Lag/Bugs erzeugen Einzelfälle) | high | flag; kick ab 10 |
| B4.4 Bewegungsplausibilität | Positionssprünge > Laufgeschwindigkeit × Zeit (+ Toleranz 1 Zelle) | ≥ 3 in 5 min | high | flag |
| B4.5 Zeitstempelfelder | Client-Tick in Paketen nicht monoton oder Drift > 2 % gegen Serverzeit | über ≥ 60 s | medium | flag (Speedhack-Indiz) |
| B4.6 Heartbeat-Bruch | Fehlende/doppelte OdinGuard-Heartbeat-Sequenzen bei sonst aktiver Verbindung | ≥ 2 in 10 min | high | kick |

## B5 – Paket-Mitleser und Overlays (RCX)

Passives Mitlesen erzeugt **kein** Serververhalten → keine Signatur, nur Begrenzung [vermutet]:

- Monster-HP und andere Zusatzwerte nur senden, wenn das Spiel sie ohnehin anzeigt
  (rAthena-Battle-Config für Monster-HP-Balken prüfen).
- Einheiten-/Positionsdaten nur im Sichtbereich senden (Standard), keine erweiterten Infos für versteckte Einheiten.
- Skill-Timer anderer Spieler sind über normale Pakete ableitbar – nicht weiter einschränkbar.

Konfidenz für jede Verhaltens-Erkennung: **keine** (low). Erkennung bleibt clientseitig (`OG-RCX-*`).

## B6 – Mehrfach-Clients (RoExt-CPU-Drosselung, Bot-Farmen)

| Signal | Messung | Startschwelle | Konfidenz | Aktion |
|---|---|---|---|---|
| B6.1 Gleichzeitige Sitzungen | Sitzungen pro Hardware-ID (OdinGuard) bzw. pro IP | > Server-Policy (z. B. 2) | high | Policy (kick der zusätzlichen Sitzung) |
| B6.2 Synchrone Aktionen | Zeitkorrelation von Aktionen mehrerer Charaktere derselben Hardware-ID | Korrelation > 0,9 über ≥ 100 Aktionen | medium | flag |

## B7 – Sicht-GRFs (dark/dark2, desu, Thoth, woerol, woe/woebg)

Transparente Bäume, veränderte Karten und entfernte Effekte verschaffen Informationsvorteile,
die serverseitig **nicht belastbar messbar** sind [vermutet]. Schwache Indizien wie „Klick auf
Ziele, die für den Original-Client verdeckt sind“ sind wegen Kamera, Zoom und Minimap nicht
fehlalarmarm. Konfidenz: low → nur clientseitige GRF-Prüfung verwenden.

## Datenerhebung (Vorschlag)

- Tabelle `odinguard_behaviour` (char_id, typ, t_ms, rtt_ms, wert) mit 7 Tagen Aufbewahrung,
  aggregierte Kennzahlen pro Tag dauerhaft.
- Auswertung asynchron (nicht im Map-Server-Hauptloop); Ergebnisse als `OG_C_DETECT`-ähnliche
  Ereignisse ins bestehende Log, Entscheidung durch GM-Review.
