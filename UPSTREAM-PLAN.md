# Upstreaming-Plan: carstenbock/open5gs → open5gs/open5gs

Stand der Analyse: **10. September 2026**. Upstream ist sehr aktiv — vor Beginn
der Arbeit die Zahlen unten neu erheben (Kommandos in Abschnitt 8).

Dieses Dokument ist die Arbeitsgrundlage für das Zerlegen des Forks in einzelne,
upstream-fähige Feature-Branches. Es ist für Claude Code gedacht, das damit
arbeitet, sowie als Gedächtnisstütze für mich.

---

## 1. Ausgangslage

| | |
|---|---|
| Fork | `carstenbock/open5gs`, Branch `main` |
| Upstream | `open5gs/open5gs`, Branch `main` |
| Merge-Base | `47eb7e806d0dbfeccd8cf97166c1087cbba2b88b` |
| Divergenz | 23 eigene Commits (ohne Merges), 116 Commits hinter Upstream |
| Umfang | ~2.400 hinzugefügte Zeilen über 58 Dateien |

Upstream hat seit der Divergenz eine **Release-19-Welle** gemergt (S1AP/NGAP
ASN.1-Migration, SBI, GTP- und PFCP-TLV-Definitionen) sowie Release v2.8.0
veröffentlicht. Das ist kein normaler Drift, sondern ein Umbau unter unseren
Änderungen hindurch.

---

## 2. Grundregeln

Diese Regeln gelten ausnahmslos für jeden Branch:

1. **`subprojects/freeDiameter.wrap` wird niemals angefasst.** Der Fork zeigt
   dort auf `carstenbock/freeDiameter`. Landet das in einem PR, bauen alle
   anderen gegen unseren Fork. Siehe Abschnitt 6.
2. **Kein Rebase von `main` als Ganzes.** Jeder Branch wird frisch von
   `origin/main` abgezweigt und der *Endzustand* der jeweiligen Änderung
   übernommen — nicht die alten Commits replayed.
3. **Ein Branch = ein Thema = ein PR.** Idealerweise 1–3 Commits.
4. **Jeder Branch muss für sich bauen und die Testsuite bestehen**, bevor er als
   fertig gilt (Abschnitt 7).
5. **Nicht pushen, keine PRs öffnen.** Branches werden lokal vorbereitet und
   vorgelegt. Das Einreichen mache ich selbst, in Abstimmung mit Sukchan.
6. Vor jedem Branch prüfen, ob Upstream das Thema inzwischen selbst gelöst hat
   (Abschnitt 8 hat die Suchkommandos).

---

## 3. Was rausfällt

Diese Änderungen werden **nicht** portiert:

| Commit | Grund |
|---|---|
| `16242793` DNS S-NAPTR PGW discovery (MME) | Überholt. Upstream hat DNS-basierte SGW/PGW-Selektion nach TS 29.303 gemergt (PR #4693: `cea217fc`, `c0f0d9ec`, `9d3f939a`, `8c37b4b7`), von Sukchan nachgehärtet in `6a479eba`. Die Upstream-Variante ist umfangreicher: asynchrones c-ares im MME-Mainloop, separates testbares Selektionsmodul `mme-dns-select.c`, Unit- und e2e-Tests mit Stub-DNS-Server. |
| `src/mme/mme-dns.c`, `src/mme/mme-dns.h`, `tests/unit/mme-dns-test.c` | Gehören zu `16242793`. Ersatzlos streichen. Die zugehörigen Hunks in `src/mme/meson.build` und `tests/unit/meson.build` ebenfalls. |
| `a98a158b` Gy-Debug-Instrumentierung rein | Hebt sich mit `b6ee26d7` auf. |
| `b6ee26d7` Gy-Debug-Instrumentierung raus | siehe oben |
| `487fed01` freeDiameter wrap URL | Fork-spezifisch, siehe Regel 1. |
| Branch `feature/pcscf-list-policy` | Vollständig in `main` enthalten. Kann gelöscht werden. |
| Branch `debug-smf-handover-7d07ab` (`33a624e3`, `b77756db`) | Der SIGABRT beim Non-3GPP→3GPP-Handover ist upstream gefixt: `a420fefa` (Sukchan, 22.06.2026, Issue #4636). Branch kann gelöscht werden. |

**Offene Aufgabe dazu:** Prüfen, ob unsere `mme-dns.c` Fälle abdeckt, die die
Upstream-Implementierung nicht behandelt. Falls ja, gehört das als Kommentar an
Issue/PR #4693 — nicht als konkurrierender PR.

---

## 4. Die Branches

Reihenfolge ist die vorgeschlagene Einreichungsreihenfolge. Klein und
unstrittig zuerst.

### 4.1 `upstream/gy-fixes` — reine Bugfixes

Quelle: `62f1aafc`, `f4a26134`, `c2e698b8`

- Vorregistrierte DCCA-Application bei der Gy-Dictionary-Initialisierung tolerieren
- `URR time_start` immer initialisieren (verhindert `CC-Time` = Unix-Epoche auf Gy)
- Gy Usage Reports für verwaiste Sessions ohne Default-Bearer absichern

*Kollisionsrisiko:* `src/smf/gy-path.c` hat upstream zwei Commits bekommen
(`5c5f3249` Session-Id-Lifetime, `8143162a` Crash bei nebenläufigem Zugriff auf
Diameter-Session-State). Vor dem Portieren ansehen.

**Idealer erster PR.** Drei klar begründbare Fixes, keine Verhaltensdiskussion.

### 4.2 `upstream/s11-release-access-bearers`

Quelle: `25125c76`

Überlappende S11 Release Access Bearers pro Transaktion beantworten.

*Kollisionsrisiko:* `src/sgwc/sxa-handler.c` hat vier Upstream-Commits
(`c0ec755d`, `45353ff6`, `32d140e9`, `21ef6bd3`). Vorher prüfen, ob
`04cfd70f` (SGWC: leere Session nach Default-Bearer-Löschung) oder `45353ff6`
das Problem bereits abdeckt.

### 4.3 `upstream/smf-ue-pool-per-upf`

Quelle: `3f3d457a`

UE-IP-Allokation an den Subnetz-Pool des jeweils gewählten UPF binden.

### 4.4 `upstream/pcscf-provisioning-policy` — der Kernbeitrag

Quelle: `3f0bcbee` plus die Testanpassungen in `tests/volte/session-test.c`,
`tests/vonr/session-test.c`, `tests/common/`

P-CSCF-Listen-Provisionierungspolicy für PCO/ePCO, konfigurierbar über
`smf.yaml`.

Upstream hat seit der Divergenz **keinen einzigen** Commit zu P-CSCF —
keine Kollision, kein konkurrierender Beitrag. Inhaltlich unser
eigenständigster Block, und mit Tests, was bei open5gs zählt.

### 4.5 `upstream/pcscf-dns-refresh`

Quelle: `5ecc18e8`

Periodische DNS-Neuauflösung der P-CSCF-FQDN-Einträge. Baut auf 4.4 auf —
Branch entsprechend auf `upstream/pcscf-provisioning-policy` basieren und im
PR-Text darauf hinweisen.

### 4.6 `upstream/pco-imcn-signalling-flag`

Quelle: `ff8f30b8`

Echo des IM-CN-Subsystem-Signaling-Flags (0x0002) im PCO.

### 4.7 `upstream/mme-emergency-numbers`

Quelle: `131f6dda`

Emergency Number List im TAU Accept, lokaler Emergency-APN.

*Achtung:* Der Hunk in `src/mme/meson.build` in diesem Commit könnte zur
gestrichenen `mme-dns.c` gehören — beim Portieren prüfen und ggf. weglassen.
*Kollisionsrisiko:* `src/mme/emm-build.c` (`3ee7e055` Network Policy nach
TS 24.301 9.9.3.52), `src/mme/esm-handler.c` (`c1f422d4`).

### 4.8 `upstream/pfcp-node-dns-refresh`

Quelle: `9c443de8`, `323524ea`

DNS-Refresh für GTP- und PFCP-Nodes; `node_id` beim DNS-Refresh zurücksetzen,
um den PFCP-Reconnect-Deadlock zu vermeiden.

### 4.9 `upstream/pfcp-reassociation`

Quelle: `9ed215a7`

PFCP-Re-Association-State-Transition für UPF, SGW-C und SMF.

### 4.10 `upstream/pfcp-restoration-cp-failure`

Quelle: `76f0a642`

PFCP-Restoration bei CP-Path-Failure (ausbleibende Heartbeats).

*Höchstes Kollisionsrisiko im ganzen Satz:* `lib/pfcp/context.c` hat seit der
Divergenz fünf Upstream-Commits (`619ce17a`, `0ad77368`, `028e1dbb`,
`32d140e9`, `5333ad2b` — letzterer die TLV-Aktualisierung auf TS 29.244
R19.5.0). 187 geänderte Zeilen auf einer stark bewegten Datei. Hier am ehesten
mit einer Neuimplementierung gegen aktuelles `origin/main` rechnen statt mit
einer Portierung.

### 4.11 `upstream/handover-ip-preservation` — **vorher abstimmen**

Quelle: `724cfb01`, `2e769a88`, `861507d8`, plus der verbleibende Teil von
`921b83f1` (Erhaltung des UPF-UE-IP-Hashes in `src/upf/context.c` und
`src/upf/n4-handler.c`)

UE-IP-Erhaltung beim WiFi→LTE-Handover, auch ohne gesetzte Handover Indication.

Wir dokumentieren selbst (`861507d8`), dass das eine Abweichung vom Standard
ist. Das braucht Sukchans Zustimmung, bevor der PR geschrieben wird — nicht als
Überraschung im Diff. Branch trotzdem vorbereiten, aber nicht einreichen.

*Hinweis:* Der Abort-Teil aus `921b83f1` (`src/smf/s5c-handler.c`,
`src/smf/gsm-sm.c`) ist durch `a420fefa` erledigt und darf nicht mitkommen.
Nur der UE-IP-Hash-Teil bleibt.

### 4.12 `upstream/diameter-dns-reresolution` — **blockiert**

Quelle: `709e2fa0`, `0aea9d12`

DNS-Neuauflösung für Diameter-Reconnects; unauflösbare Hostnamen tolerieren.

Hängt funktional an Änderungen in freeDiameter (Abschnitt 6). Erst angehen,
wenn die freeDiameter-Seite geklärt ist. Branch vorbereiten, aber als blockiert
markieren.

---

## 5. Kollisionsübersicht

Dateien, die wir ändern und die Upstream seit der Divergenz ebenfalls angefasst
hat:

| Datei | Upstream-Commits |
|---|---|
| `src/smf/gsm-sm.c` | 8 |
| `src/smf/context.c` | 6 |
| `lib/pfcp/context.c` | 5 |
| `src/smf/n4-handler.c` | 5 |
| `src/sgwc/sxa-handler.c` | 4 |
| `src/smf/s5c-handler.c` | 2 |
| `src/smf/gy-path.c` | 2 |
| `src/mme/emm-build.c` | 2 |
| `src/upf/n4-handler.c` | 2 |
| `src/mme/esm-handler.c` | 1 |

---

## 6. freeDiameter — eigener Strang

`subprojects/freeDiameter.wrap` zeigt im Fork auf
`carstenbock/freeDiameter`, Branch `master`, statt auf
`open5gs/freeDiameter`, Branch `r1.5.0`.

Die beiden haben **keine gemeinsame Merge-Base**: unser Fork sitzt auf dem
originalen freeDiameter, nicht auf Sukchans Zweig. Darauf liegen acht eigene
Commits:

- `016370d` Dockerfile und Build-Skript
- `363bf28` `.gitignore`
- `b4437d6` DNS-Neuauflösung für `ConnectTo`-Hostnamen in der Peer-Konfiguration
- `2cda442` Debian-Abhängigkeiten (libgnutls, libgcrypt)
- `33f5136` Meson-Build für die open5gs-Subproject-Integration
- `c8f77b4` libidn2 statt libidn im Meson-Build
- `412b76d` Hostname-Auflösung in der Peer-Konfiguration
- `c9dac06` PSM-Restart-Mechanismus für persistente Peers

Das ist ein eigenes Gespräch — entweder mit Sukchan über
`open5gs/freeDiameter`, oder mit dem freeDiameter-Projekt selbst. Bis dahin
bleibt 4.12 blockiert. **Für Claude Code: an diesem Repo nichts tun**, außer es
wird ausdrücklich angewiesen.

---

## 7. Arbeitsablauf pro Branch

```bash
# 1. Frisch von aktuellem Upstream abzweigen
git fetch upstream
git switch -c upstream/<thema> upstream/main

# 2. Endzustand übernehmen, nicht Commits replayen
git checkout <fork-main> -- <ganz neue Dateien>
git checkout --patch <fork-main> -- <gemischte Dateien>

# 3. Sichten, committen
git add -p
git commit

# 4. Bauen und testen
meson setup build --reconfigure
ninja -C build
meson test -C build
```

Erst wenn Build und Tests grün sind, gilt der Branch als fertig. Ergebnis pro
Branch kurz festhalten: welche Hunks übernommen, welche verworfen, ob der Test
lief.

Wenn beim Portieren auffällt, dass Upstream das Problem inzwischen anders
gelöst hat: Branch verwerfen und in Abschnitt 3 nachtragen.

---

## 8. Analyse neu erheben

```bash
git remote add upstream https://github.com/open5gs/open5gs.git   # falls nötig
git fetch upstream

# Divergenz (links: Upstream-only, rechts: Fork-only)
git rev-list --left-right --count upstream/main...main
git merge-base upstream/main main

# Eigene Commits
git log --oneline --no-merges --date=short \
  --pretty='%h %ad | %s' upstream/main..main

# Eigenes Delta auf Dateiebene (drei Punkte!)
git diff --stat upstream/main...main

# Kollisionen: Upstream-Commits auf einer von uns geänderten Datei
git log --oneline main..upstream/main -- <datei>

# Hat Upstream unser Thema schon gelöst?
git log --no-merges -i --pretty='%h %ad %s' --date=short \
  main..upstream/main --grep='<stichwort>'
```

---

## 9. Kontext

Gesprächspartner upstream ist Sukchan Lee (`acetcom@gmail.com`). Er nimmt
externe Beiträge an — PR #4693 kam von außen — härtet sie danach aber selbst
nach. Damit ist zu rechnen: der Code wird nach dem Merge noch angefasst.

Parallel laufen dieselben Aufteilungen für `carstenbock/pyhss-1` (12 Themen)
und `carstenbock/opensips-1` (rund 34 Themen). Jedes Repo hat sein eigenes
Dokument.
