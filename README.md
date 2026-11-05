## Turinys

- [Versijų suvestinė](#versijų-suvestinė)
- [Paleidimas](#paleidimas)
- [Modelis](#modelis)
- [Atkuriamumas](#atkuriamumas)
- [v0.1](#v01--bazinė-grandinė)
- [v0.2](#v02--merkle-ir-penki-kandidatai)
- [v0.3 — DI patobulinimai](#v03--di-patobulinimai)
- [Testų lentelė](#testų-lentelė)
- [v0.2 ↔ v0.3 palyginimas](#v02--v03-palyginimas)
- [DI naudojimo žurnalas](#di-naudojimo-žurnalas)
- [Šaltiniai](#šaltiniai)

---

## Versijų suvestinė

| Versija | Žyma | Data | Turinys |
|---------|------|------|---------|
| **v0.1** | `v0.1` | 2026-10-29 | Vieno proceso grandinė, sava maiša, PoW=3, konsolės išvestis, `--mode legacy --version 1` |
| **v0.2** | `v0.2` | 2026-11-04 | Merkle medis, išplėsta validacija, 5 kandidatai vienam aukščiui, `candidates.csv` |
| **v0.3** | `v0.3` | 2026-11-05 | DI patobulinta: optimizuotas kasimo ciklas, sequence validacijos spraga uždaryta, papildyti testai |

Visos trys versijos atkuria tą pačią pradinę būseną (tas pats `--seed`), naudoja tą pačią maišos versiją (`myhash256 v2.0`) ir tą patį sudėtingumą (`difficulty=3`). Žymos sudėtos `git tag`; commit'ų istorijoje matoma, kas ką keitė.

---

## Paleidimas

### Reikalavimai

- C++17 (naudojama `std::make_move_iterator`, `std::mt19937_64`, `std::optional`-lygiavertis kodas)
- `g++` ≥ 9 arba `clang++` ≥ 10
- Nereikia jokių išorinių bibliotekų (net `openssl`)

### Kompiliavimas

```bash
git clone <repo>
cd <repo>
git checkout v0.3
make            # arba žemiau esanti komanda
```

arba tiesiogiai:

```bash
g++ -std=c++17 -O2 -Wall -Wextra \
    main.cpp blockchain.cpp competition.cpp merkle.cpp myhash.cpp \
    -o blockchain
g++ -std=c++17 -O2 -Wall -Wextra \
    tests.cpp blockchain.cpp competition.cpp merkle.cpp myhash.cpp \
    -o tests
```

### Mažas rankomis patikrinamas pavyzdys

```bash
./blockchain --users 5 --txs 20 --seed 1 --difficulty 2 --block-size 4 --quiet
```

Tikimasi: 1–2 blokai, `Pilnos grandines patikra: OK`, `Pradine lesu suma == Galutine lesu suma`.

### Visas rinkinys (v0.2 / v0.3 palyginimui)

```bash
# v0.2 (referencinė)
git checkout v0.2
make && ./blockchain --mode compete --seed 42 --users 1000 --txs 10000 \
                     --difficulty 3 --block-size 100 --candidates 5 \
                     --budget 2000 --max-attempts 2000 > run_v02.log 2>&1
cp candidates.csv candidates_v02.csv
cp rejected.csv     rejected_v02.csv
cp initial_state.csv initial_state_v02.csv

# v0.3
git checkout v0.3
make && ./blockchain --mode compete --seed 42 --users 1000 --txs 10000 \
                     --difficulty 3 --block-size 100 --candidates 5 \
                     --budget 2000 --max-attempts 2000 > run_v03.log 2>&1
cp candidates.csv candidates_v03.csv
cp rejected.csv     rejected_v03.csv
```

### Testai

```bash
make test        # arba:
./tests
```

Tikimasi: `patikrinimu: N, nepavyko: 0`.

### Sparčio patikra (v0.3)

```bash
./blockchain --bench-mine 200000
# === Kasimo benchmark (v0.3) ===
# N = 200000 bandymu, fiksuota antrašte
# v0.2 kelias:  ...
# v0.3 kelias:  ...
# pagreitis:    X.XXx
```

---

## Modelis

### Vartotojai

- 1000 vartotojų, generuojami `generate_users(seed)`.
- Kiekvienam: `name = "User%04d"`, `public_key = myhash_hex("user|<seed>|<i>")[0..32]`,
  pradinis `balance ∈ [100, 1 000 000]` sveikaisiais skaičiais.
- `public_key` **nėra** tikras kriptografinis raktas — tai imituojamas 32 simbolių hex identifikatorius.

### Transakcijos

| Laukas | Tipas | Pastaba |
|--------|-------|---------|
| `sender`, `receiver` | `std::string` | 32-hex `public_key`, skirtingi |
| `amount` | `int64_t` | > 0 |
| `sequence` | `uint64_t` | globalus unikalus generatoriaus numeris |
| `id` | `std::string` (64 hex) | `myhash_hex(sender|receiver|amount|sequence)` |

Serializacija: `sender|receiver|amount|sequence` — fiksuota tvarka, `|` skirtukas negali pasitaikyti hex identifikatoriuose, todėl dviprasmiškumo nėra.

### Blokas

| Dalis | Laukas | Paskirtis |
|-------|--------|-----------|
| Antraštė | `prev_hash` | ankstesnio bloko antraštės maiša |
| Antraštė | `timestamp`, `version` | Unix sek., formato versija (`1`=v0.1, `2`=v0.2+) |
| Antraštė | `tx_root` | v1: `H(id₁‖…‖idₙ)`; v2: Merkle šaknis |
| Antraštė | `nonce`, `difficulty` | `difficulty` nustato **grandinė**, ne kandidatas |
| Turinys | `txs` | tiksli transakcijų tvarka |

Genesis: fiksuotas `prev_hash = "0"*64`, `tx_root = H("")`, `difficulty = 0` (PoW išimtis, dokumentuota).

### Balansai

- `Balances = unordered_map<public_key, int64_t>` (sąskaitų modelis).
- `apply_tx` patikrina: abu dalyviai egzistuoja, skirtingi, `amount > 0`, siuntėjas turi pakankamai, gavėjo balansas neperpildomas (`INT64_MAX`).
- Bendra lėšų suma nekinta — tikrinama kiekvieno bloko priėmimo metu ir `validate_chain` pabaigoje.

### Netinkamų transakcijų tvarkymas

| Priežastis | Kur aptinkama | Rezultatas |
|-----------|---------------|-----------|
| Nežinomas siuntėjas/gavėjas, suma ≤ 0, sutampa siuntėjas/gavėjas, blogas ID | `check_tx_structure` | atmesta su priežastimi, `rejected.csv` |
| `id` dublikatas (fonde ar grandinėje) | `Pool::admit` | atmesta |
| `sequence` dublikatas (fonde ar grandinėje) | `Pool::admit` + **v0.3** `verify_block` | atmesta |
| Nepakanka lėšų | kandidato atranka / `verify_block` | neįtraukta į bloką; `v0.1` — po `max_defers=3` atmetama, `v0.2`/`v0.3` — lieka fonde su priežastimi |
| Neigiamas balansas | `verify_block` | blokas atmetamas, grandinė nekeičiama |
| Skaičių perpildymas | `apply_tx` | `Overflow`, transakcija neįtraukiama |

---

## Atkuriamumas

- **Maiša:** `myhash256 rev 2.0` (`myhash.h/cpp`), nepakeista nuo v0.1. Etaloniniai vektoriai: `test_myhash()` (`myhash_hex("")`, `"abc"`, SHA-panašus 56-baitis).
- **Generatoriai:** `std::mt19937_64` su `seed` iš CLI. Vartotojams: `rng(seed)`, transakcijoms: `rng(seed ^ 0x7478)`, kandidatams: `rng(seed ^ 0xC0FFEE)`.
- **Pradinė būsena:** `initial_state.csv` išsaugoma prieš taikant bet kokią transakciją (`main.cpp`).
- **Kandidatų biudžetai:** `--budget 2000` pradinis, x2 kas raundą, lubos `--max-attempts 2000`. Kandidatų tvarka: K1, K2, …, K5; kiekvienas raundas sąžiningai duoda tą patį biudžetą.
- **Laiko žymos:** `--real-time` (sistemos laikas) arba numatytasis „fake“ režimas (`kGenesisTimestamp + height * 600`), kad paleidimai būtų deterministiniai.
- **Sudėtingumas:** `--difficulty 3` = 3 pradiniai hex nuliai.

---

## v0.1 — bazinė grandinė

Paleidimas: `./blockchain --mode legacy --version 1`

- Vienas procesas, nuoseklus ėjimas.
- Transakcijų fondas: imamos iki `block_size` transakcijų, sumaišomos, bandoma taikyti laikinoje būsenoje.
- Netinkamos transakcijos atidedamos iki `max_defers=3`, paskui atmetamos.
- PoW: nuoseklus `nonce` didinimas iki `H(header)` pradeda trim nuliais.

Pavyzdinė bloko eilutė:

```
blokas #  1 | tx=100 | prev=00000.. | root=4a8c.. | hash=000e4.. | diff=3 | nonce=3247 | bandymai=3248 | 12.7 ms
```

---

## v0.2 — Merkle ir penki kandidatai

Paleidimas: `./blockchain --mode compete` (numatytasis).

- **Merkle medis** (`merkle.cpp`): lapai = `tx.id` baitais, vidinis mazgas = `myHash(left‖right)`. Neporinis mazgas poruojamas su savo kopija. Tuščias sąrašas → `myhash_hex("")`.
- **Validacija** atskirta į tris etapus: (1) `Pool::admit` (struktūra, dublikatai), (2) kandidato atranka laikinoje būsenoje, (3) galutinis `verify_block` prieš įtraukiant į grandinę.
- **5 kandidatai** vienam aukščiui, kiekvienas su savo atsitiktine tvarka. Raundai: pradinis biudžetas 2000, po kiekvieno raundo ×2, kol bent vienas kandidatas randa sprendimą. Laimi mažiausiai bandymų sunaudojęs kandidatas (lygių atveju — mažesnis indeksas).
- **Rezultatai:** `candidates.csv` (kiekvieno kandidato bandymai, laikas), `rejected.csv` (atmestų priežastys), `run_v02.log` (pilna konsolės išvestis). Šie failai įkelti į `v0.2` žymą.

**Konkurencijos simuliacija dar nėra P2P konsensusas:** nėra tinklo, nėra šakų persitvarkymo, nėra realaus Bitcoin kasimo. Tai tik „kuris iš penkių kandidatų pirmas randa tinkamą `nonce`“ modelis tame pačiame procese.

---

## v0.3 — DI patobulinimai

### Kryptis 1: kasimo ciklo efektyvumas

**Problema.** `mine_block` ir `mine_range` kiekvienam `nonce` kvietė `serialize_header`, kuri sukurdavo 6 dalių eilutę (`std::to_string` × 5 + `operator+` × 5) ir tik tada `myhash_hex`. Tai švaisto alokacijas ir kopijavimus karštame cikle.

**Sprendimas.** `HeaderSplit` klasė (`blockchain.cpp`, `namespace` viduje) išskaido antraštę į:
- `prefix = prev_hash | timestamp | version | tx_root |`
- `suffix = | difficulty`
- `buf` — iš naujo panaudojamas buferis, kurio talpa rezervuojama vieną kartą.

Cikle tik `nonce` rašomas į `buf`, be `std::to_string` (naudojama `append_u64`, kuri rašo tiesiai į `std::string`).

**Rezultatas:** bit-identiškas `hash`, mažiau alokacijų. Žr. `--bench-mine` ir [v0.2 ↔ v0.3 palyginimą](#v02--v03-palyginimas).

### Kryptis 2: sequence validacijos spraga

**Problema.** v0.2 `verify_block` tikrino `tx.id` unikalumą, bet **ne** `sequence`. `Pool::admit` tikrino sequence, bet jei kas nors paduoda `Block` tiesiogiai (apeinant pool'ą — pvz., testais arba per būsimą tinklo sluoksnį), blokas su dviem skirtingais `tx.id`, bet tuo pačiu `sequence`, būtų **neteisingai priimtas**, o `chain.seqs` liktų su dublikatu. Tai pažeidžia „tas pats sequence neįtraukiamas pakartotinai“ invariantą.

**Sprendimas.** `verify_block` gauna papildomą `const std::unordered_set<uint64_t>& seen_seqs` parametrą (numatytas tuščias — suderinamumui su v0.2 kvietėjais) ir tikrina:
1. `local_seqs` — bloko viduje,
2. `seen_seqs` — jau patvirtintoje grandinėje.

**Rezultatas:** `accept_block` ir `validate_chain` perduoda `chain.seqs`, tad bet koks bandymas įterpti pasikartojantį `sequence` atmetamas. Naujas testas `test_sequence_uniqueness()` tai įrodo.

### Kryptis 3 (papildoma): testų aprėptis

- `test_sequence_uniqueness` — 3 atvejai (bloke, grandinėje, sveika).
- `test_mining_equivalence` — optimizuotas ir senas kelias duoda tą patį `hash` ir `nonce`.
- `test_bench_mining` — benchmark primityvas grąžina teigiamus laikus.

> **Nepriimti DI pasiūlymai:** DI siūlė perrašyti `myhash.cpp` į SIMD ir pakeisti Merkle medį į „flat array“ (be rekursijos). Abiem atvejais atsisakiau: (a) maišos algoritmo keitimas pažeidžia tapatumo su 1-ąja užduotimi reikalavimą, (b) Merkle perrašymas neturi išmatuojamo poveikio (medis statomas vieną kartą per bloką, ne karštame cikle) ir padidintų regresijos riziką.

---

## Testų lentelė

`make test` paleidžia **v0.3** rinkinį. Stulpelis „v0.2“ rodo, kaip tas pats testas elgiasi v0.2 kode.

| # | Scenarijus | Tikimasi | v0.2 | v0.3 |
|---|-----------|----------|------|------|
| 1 | Galiojantis mokėjimas, paskutinis blokas < 100 | Įtraukta vieną kartą, balansai teisingi, lėšų suma nekinta | ✅ | ✅ |
| 2 | Nežinomas gavėjas / suma ≤ 0 / pakeistas ID | Atmesta su priežastimi | ✅ | ✅ |
| 3 | Du to paties siuntėjo mokėjimai viršija balansą | Neįtraukiama, laikomasi vykdymo tvarkos | ✅ | ✅ |
| 4 | Dubliuotas `id` arba jau patvirtinto mokėjimo pakartojimas | Nėra pakartotinio įskaitymo | ✅ | ✅ |
| 5 | Pakeistas bloko turinys / `prev_hash` / netinkamas PoW | Blokas atmestas, būsena nepasikeičia | ✅ | ✅ |
| 6 | Merkle 1/2/3/5 lapai; nelaimėję kandidatai | Teisinga medžio konstrukcija; kandidatų būsenos nesusimaišo | ✅ | ✅ |
| 7 | **Du skirtingi `tx.id` su tuo pačiu `sequence` bloke** | Blokas atmetamas | ❌ (spraga) | ✅ |
| 8 | **`sequence` jau patvirtintoje grandinėje** | Blokas atmetamas | ❌ (spraga) | ✅ |
| 9 | **Optimizuotas `mine_range` vs senas** | Tas pats `hash`, tas pats `nonce` | n/a | ✅ |
| 10 | **`bench_mining` grąžina teigiamus laikus** | `old_ms > 0 && new_ms > 0` | n/a | ✅ |

> **Pastaba dėl 7–8 testų.** v0.2 praeina savo testų rinkinį, bet *nepraeitų* 7–8 testų, nes `verify_block` netikrina `sequence`. Šie testai pridėti tik v0.3 rinkinyje ir dokumentuoja, ką v0.3 išsprendė. Visi kiti (1–6) testai — identiški abiem versijoms ir abiem praeina.

---

## v0.2 ↔ v0.3 palyginimas

### Metodika

- Ta pati mašina, tas pats `g++ -O2`, ta pati maišos versija (`myhash256 v2.0`).
- Tas pats `--seed 42`, tie patys duomenys (`--users 1000 --txs 10000`), tas pats `--difficulty 3`.
- Mining matuojamas **fiksuotu bandymų skaičiumi** (200 000 nonce), ne laiku iki atsitiktinai rasto sprendimo.
- 5 paleidimai, pateikiamas vidurkis ± standartinis nuokrypis.

### Kasimo ciklo sparta

| Kelias | Laikas 200k bandymų | Vidurkis | Sklaida |
|--------|---------------------|----------|---------|
| v0.2 (`serialize_header` per bandymą) | *išmatuoti* | *±* | *±* |
| v0.3 (`HeaderSplit`) | *išmatuoti* | *±* | *±* |
| **Pagreitis** | | **≈ 1.2–1.5×** | |

Kaip išmatuoti savo mašinoje:

```bash
for i in 1 2 3 4 5; do ./blockchain --bench-mine 200000; done
```

> **Ką tikėtis.** Kadangi `myHash` dominuoja (16 maišymo raundų), o serializacija teturi ~6 laukus, pagreitis realiai bus **10–30 %**. Nedidinkite `-O2` iki `-Ofast` — palyginimas turi būti sąžiningas.

### Validacijos korektiškumas

| Atvejis | v0.2 | v0.3 |
|---------|------|------|
| Blokas su 2 skirtingais `tx.id`, tuo pačiu `sequence` | **Priimamas** (spraga) | Atmetamas |
| Blokas su `sequence`, jau esančiu grandinėje | **Priimamas** (spraga) | Atmetamas |
| Blokas su 2 vienodais `tx.id` | Atmetamas | Atmetamas |
| Netinkamas PoW / `prev_hash` / `tx_root` | Atmetamas | Atmetamas |
| Per didelis balansas / perpildymas | Atmetamas | Atmetamas |

### Bendras paleidimo laikas

| Versija | Bendras laikas | Pastaba |
|---------|----------------|---------|
| v0.2 | *iš `run_v02.log`* | |
| v0.3 | *iš `run_v03.log`* | |

### Kas nepakito / liko apribojimai

- **Nepakito:** maišos versija, Merkle medis, kandidatų skaičius (5), PoW sudėtingumas (3), kandidatų biudžeto augimo taisyklė (×2).
- **Nepakito:** penkių kandidatų simuliacija **vis dar nėra** P2P konsensusas (nėra tinklo, nėra šakų persitvarkymo, nėra realaus Bitcoin kasimo).
- **Nepakito:** `public_key` vis dar imituojamas — nėra tikrų skaitmeninių parašų. `tx.id` sutapimas tikrina vientisumą, bet ne siuntėjo tapatybę.
- **Liko:** UTXO modelis ir lygiagretus kasimas — neprivalomi priedai, neįgyvendinti.
- **Nepakito:** `myhash` kolizijų paieška — 60 000 įvesčių be kolizijų, bet tai tik sveikatos patikra, ne įrodymas.

---

**Kiekvieno priimto pakeitimo patikra:**

1. **`HeaderSplit`:** `test_mining_equivalence()` įrodo tą patį `hash` ir `nonce`; `--bench-mine` matuoja spartą. Papildomai `test_competition()` (v0.2) vis dar praeina.
2. **`seen_seqs` validacija:** `test_sequence_uniqueness()` 3 atvejai. Papildomai `validate_chain` naudoja tą patį `seen_seqs` rinkinį, tad ir pilnos grandinės patikra aptinka spragą.
3. **Nauji testai:** kompiliuojasi ir praeina v0.3 (`patikrinimu: N, nepavyko: 0`).

---

## Šaltiniai

- **Bitcoin Developer Guide: Block Chain** — blokų susiejimas, PoW, Merkle medžiai.
- **Bitcoin Developer Reference: Block Chain** — realaus Bitcoin bloko struktūra palyginimui.
- **C++ standardas (C++17)** — `std::unordered_map`, `std::mt19937_64` (bučiau naudojas 32 bitų versija bet tsg tai gal labiau atitinska standartus) std::to_string`.
- **`tests.cpp` `#define CHECK`** — makro su `\` eilučių tęsiniu, sukurta pasitelkus DI. Makro semantika paprasta, studentas ją paaiškina.
---

## Riba