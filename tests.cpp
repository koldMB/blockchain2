#include <iostream>
#include <set>

#include <cstdint>
#include <stdexcept>

#include "blockchain.h"
#include "merkle.h"
#include "myhash.h"

using namespace bc;

static int g_checks = 0, g_failures = 0; // nežinau kodėl taip veikia bet be \ jis kažkodėl atsisako veikt, turėjau bsk su DI pagalbos dėl šito
#define CHECK(c)                                                                 \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(c)) {                                                              \
            ++g_failures;                                                        \
            std::cerr << "  nepavyko " << __FILE__ << ":" << __LINE__ << "  " #c "\n"; \
        }                                                                        \
    } while (0)

static Config small_cfg(uint32_t diff = 2) {
    Config c;
    c.difficulty = diff;
    c.verbose = false;
    return c;
}

static void test_myhash() {
    std::cout << "[myhash etaloniniai vektoriai]\n";
    CHECK(myhash_hex("") ==
      "78ebe000d4784925850fa5e45e536863ea7d7aa424d13a81d656b54d43583a90");
    CHECK(myhash_hex("abc") ==
      "746edecc82d44fffcc9e612ed45c1fd36177288a824654959979c36f9c7f2501");
    CHECK(myhash_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
      "c4bf5e3b1960a75ac4c9b859181e0d8c812aa051f53870a9cb3a5e3e2013f788");
    CHECK(myhash_hex("abc").size() == 64);
    CHECK(myhash_hex("abc") != myhash_hex("abd"));
    CHECK(myhash_hex(std::string("abc")) != myhash_hex(std::string("abc") + '\0'));
}

static void test_serialization() {
    std::cout << "[serializacija ir transakciju id]\n";
    CHECK(serialize_tx("a12f", "b34e", 100, 5) == "a12f|b34e|100|5");
    CHECK(serialize_tx("a", "b", 1, 23) != serialize_tx("a", "b", 12, 3));
    Transaction t1 = make_tx("a12f", "b34e", 100, 5);
    Transaction t2 = make_tx("a12f", "b34e", 100, 5);
    Transaction t3 = make_tx("a12f", "b34e", 100, 6);
    CHECK(t1.id == t2.id);
    CHECK(t1.id != t3.id);
    CHECK(t1.id == myhash_hex("a12f|b34e|100|5"));
    CHECK(t1.id.size() == 64);
}

static void test_genesis_and_header() {
    std::cout << "[genesis ir antraštes hash determinizmas]\n";
    Block g = make_genesis();
    CHECK(g.header.prev_hash == std::string(64, '0'));
    CHECK(g.header.tx_root == myhash_hex(""));
    CHECK(g.header.tx_root == compute_tx_root({}));
    CHECK(g.hash == make_genesis().hash);
    CHECK(g.hash.size() == 64);
    BlockHeader h{std::string(64, 'a'), 1000, 1, std::string(64, 'b'), 7, 3};
    CHECK(compute_header_hash(h) == compute_header_hash(h));
    CHECK(serialize_header(h) == std::string(64, 'a') + "|1000|1|" + std::string(64, 'b') + "|7|3");
    BlockHeader h2 = h;
    h2.nonce = 8;
    CHECK(compute_header_hash(h) != compute_header_hash(h2));
    CHECK(meets_difficulty("000abc", 3));
    CHECK(!meets_difficulty("00abc0", 3));
}

static void test_hand_case() {
    std::cout << "[mazas rankomis patikrinamas atvejis]\n";
    std::vector<User> us = {{"A", "aaaa", 1000}, {"B", "bbbb", 500}, {"C", "cccc", 0}};
    std::vector<Transaction> txs = {make_tx("aaaa", "bbbb", 300, 0), make_tx("bbbb", "cccc", 600, 1),
                                    make_tx("cccc", "aaaa", 50, 2)};
    SimResult r = run_simulation(us, txs, small_cfg(), nullptr);
    CHECK(r.included == 3);
    CHECK(r.rejected.empty() && r.remaining.empty());
    CHECK(r.final_state["aaaa"] == 750);
    CHECK(r.final_state["bbbb"] == 200);
    CHECK(r.final_state["cccc"] == 550);
    CHECK(r.initial["aaaa"] == 1000);
}

static void test_no_overspend_in_block() {
    std::cout << "[negalima isleisti daugiau nei turi viename bloke]\n";
    std::vector<User> us = {{"A", "aaaa", 100}, {"B", "bbbb", 0}, {"C", "cccc", 0}};
    std::vector<Transaction> txs = {make_tx("aaaa", "bbbb", 60, 0), make_tx("aaaa", "cccc", 60, 1)};
    SimResult r = run_simulation(us, txs, small_cfg(), nullptr);
    CHECK(r.included == 1);
    CHECK(r.rejected.size() == 1);
    CHECK(!r.rejected[0].reason.empty());
    CHECK(r.chain.size() == 2 && r.chain[1].txs.size() == 1);
    CHECK(r.final_state["aaaa"] == 40);
    for (const auto& kv : r.final_state) CHECK(kv.second >= 0);
}

static void test_partial_last_block() {
    std::cout << "[paskutinis nepilnas blokas]\n";
    std::vector<User> us = {{"A", "aaaa", 1000000}, {"B", "bbbb", 1000000}};
    std::vector<Transaction> txs;
    for (uint64_t i = 0; i < 250; ++i) txs.push_back(make_tx("aaaa", "bbbb", 1, i));
    SimResult r = run_simulation(us, txs, small_cfg(), nullptr);
    CHECK(r.chain.size() == 4);
    CHECK(r.chain[1].txs.size() == 100);
    CHECK(r.chain[2].txs.size() == 100);
    CHECK(r.chain[3].txs.size() == 50);
    CHECK(r.included == 250);
}

static void test_determinism() {
    std::cout << "[tas pats ivedis -> ta pati grandine]\n";
    Config c = small_cfg();
    auto us1 = generate_users(50, 7), us2 = generate_users(50, 7);
    auto t1 = generate_transactions(us1, 300, 7, true), t2 = generate_transactions(us2, 300, 7, true);
    CHECK(t1.size() == t2.size());
    for (size_t i = 0; i < t1.size(); ++i) CHECK(t1[i].id == t2[i].id);
    c.seed = 7;
    SimResult a = run_simulation(us1, t1, c, nullptr), b = run_simulation(us2, t2, c, nullptr);
    CHECK(a.chain.size() == b.chain.size());
    for (size_t i = 0; i < a.chain.size() && i < b.chain.size(); ++i) CHECK(a.chain[i].hash == b.chain[i].hash);
}

static void test_tampering() {
    std::cout << "[sugadintos grandines aptikimas]\n";
    Config c = small_cfg();
    c.seed = 11;
    auto us = generate_users(60, 11);
    auto txs = generate_transactions(us, 400, 11, false);
    SimResult r = run_simulation(us, txs, c, nullptr);
    std::string err;
    CHECK(r.chain.size() > 3);
    CHECK(validate_chain(r.chain, r.initial, c.difficulty, err));

    auto bad = r.chain;
    bad[2].txs[0].amount += 1;
    CHECK(!validate_chain(bad, r.initial, c.difficulty, err));

    bad = r.chain;
    bad[2].header.nonce += 1;
    CHECK(!validate_chain(bad, r.initial, c.difficulty, err));

    bad = r.chain;
    bad[2].txs.pop_back();
    CHECK(!validate_chain(bad, r.initial, c.difficulty, err));

    bad = r.chain;
    bad[3].header.prev_hash = std::string(64, '1');
    CHECK(!validate_chain(bad, r.initial, c.difficulty, err));

    bad = r.chain;
    for (uint64_t n = 0;; ++n) {
        bad[2].header.nonce = n;
        std::string h = compute_header_hash(bad[2].header);
        if (!meets_difficulty(h, c.difficulty)) { bad[2].hash = h; break; }
    }
    CHECK(!validate_chain(bad, r.initial, c.difficulty, err));

    bad = r.chain;
    bad[3].txs[0] = bad[2].txs[0];
    CHECK(!validate_chain(bad, r.initial, c.difficulty, err));
}

// runs a full simulation with many users and transactions and checks balances transaction counts rejected transactions and block validity
static void test_full_run_invariants() {
    std::cout << "[invariantai paleidime]\n"; // https://www.cyfrin.io/glossary/invariant
    Config c = small_cfg();
    c.seed = 99;
    auto us = generate_users(200, 99);
    auto txs = generate_transactions(us, 2000, 99, true);
    SimResult r = run_simulation(us, txs, c, nullptr);

    CHECK(r.supply_before == r.supply_after);
    for (const auto& kv : r.final_state) CHECK(kv.second >= 0);
    CHECK(r.included + r.rejected.size() + r.remaining.size() == r.total_input);
    CHECK(r.remaining.empty());
    CHECK(r.rejected.size() >= 8);
    for (const auto& x : r.rejected) CHECK(!x.reason.empty());

    std::set<std::string> ids;
    size_t counted = 0;
    for (size_t i = 1; i < r.chain.size(); ++i) {
        CHECK(r.chain[i].hash.size() == 64);
        CHECK(r.chain[i].hash.substr(0, c.difficulty) == std::string(c.difficulty, '0'));
        for (const auto& tx : r.chain[i].txs) { ids.insert(tx.id); ++counted; }
    }
    CHECK(ids.size() == counted);
    CHECK(counted == r.included);
    std::string err;
    CHECK(validate_chain(r.chain, r.initial, c.difficulty, err));

    Balances acc = {{"aaaa", 10}, {"bbbb", 10}};
    CHECK(!check_tx_structure(make_tx("zzzz", "bbbb", 1, 0), acc).empty());
    CHECK(!check_tx_structure(make_tx("aaaa", "aaaa", 1, 0), acc).empty());
    CHECK(!check_tx_structure(make_tx("aaaa", "bbbb", 0, 0), acc).empty());
    CHECK(check_tx_structure(make_tx("aaaa", "bbbb", 1, 0), acc).empty());
}

// v0.2 testai
using Bytes = std::vector<uint8_t>;
static Bytes dec(const std::string& hex) { return hex_to_bytes(hex); }
static Bytes H2(const Bytes& l, const Bytes& r) {  // myhash(l || r) tiesiogiai per myHash, nenaudojant merkle_root
    Bytes c = l;
    c.insert(c.end(), r.begin(), r.end());
    const hash256 h = myHash(c.data(), c.size());
    Bytes out;
    for (int k = 0; k < 8; ++k)
        for (int sh = 24; sh >= 0; sh -= 8) out.push_back(static_cast<uint8_t>(h.w[k] >> sh));
    return out;
}
static std::string hx(const Bytes& b) { return bytes_to_hex(b); }
static std::vector<std::string> leaf_ids(int n) {
    std::vector<std::string> v;
    for (int i = 0; i < n; ++i) v.push_back(myhash_hex("tx" + std::to_string(i)));
    return v;
}


static void test_myhash_extra() {
    std::cout << "[myhash: baitu API sutampa su tekstiniu, jautrumas, kolizijos]\n";
    CHECK(myhash_hex_bytes(std::vector<uint8_t>{'a', 'b', 'c'}) == myhash_hex("abc"));
    CHECK(myhash_hex_bytes({}) == myhash_hex(""));
    CHECK(hex_to_bytes("00ff10").size() == 3 && bytes_to_hex(hex_to_bytes("00ff10")) == "00ff10");
    bool threw = false;
    try { hex_to_bytes("0G"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    CHECK(myhash_hex("ab") != myhash_hex("ba"));  // tvarka svarbi
    // 60 000 skirtingu ivesciu - koliziju nerasta (sveikatos patikra, ne irodymas)
    std::set<std::string> hs;
    for (int i = 0; i < 60000; ++i) hs.insert(myhash_hex("coll|" + std::to_string(i)));
    CHECK(hs.size() == 60000);
}

static void test_merkle_cases() {
    std::cout << "[merkle: 0, 1, 2, 3, 4, 5 lapu, ranka skaiciuojami]\n";
    CHECK(merkle_root({}) == myhash_hex(""));  // tuscias (genesis) atvejis apibreztas atskirai
    auto v1 = leaf_ids(1);
    CHECK(merkle_root(v1) == v1[0]);  // vienas lapas = saknis
    auto v2 = leaf_ids(2);
    CHECK(merkle_root(v2) == hx(H2(dec(v2[0]), dec(v2[1]))));
    CHECK(merkle_root(v2) != myhash_hex(v2[0] + v2[1]));  // hex dekoduojamas i baitus, ne jungiamas kaip tekstas
    auto v3 = leaf_ids(3);
    CHECK(merkle_root(v3) == hx(H2(H2(dec(v3[0]), dec(v3[1])), H2(dec(v3[2]), dec(v3[2])))));
    auto v4 = leaf_ids(4);
    CHECK(merkle_root(v4) == hx(H2(H2(dec(v4[0]), dec(v4[1])), H2(dec(v4[2]), dec(v4[3])))));
    auto v5 = leaf_ids(5);
    Bytes ab = H2(dec(v5[0]), dec(v5[1])), cd = H2(dec(v5[2]), dec(v5[3])), ee = H2(dec(v5[4]), dec(v5[4]));
    CHECK(merkle_root(v5) == hx(H2(H2(ab, cd), H2(ee, ee))));
    // tx_root v2 = Merkle, v1 = konkatenacija
    std::vector<Transaction> txs = {make_tx("aa", "bb", 1, 0), make_tx("bb", "cc", 2, 1), make_tx("cc", "aa", 3, 2)};
    CHECK(compute_tx_root(txs, kVersionMerkle) == merkle_root({txs[0].id, txs[1].id, txs[2].id}));
    CHECK(compute_tx_root(txs, kVersionLegacy) == myhash_hex(txs[0].id + txs[1].id + txs[2].id));
    CHECK(compute_tx_root(txs, kVersionMerkle) != compute_tx_root(txs, kVersionLegacy));
    bool threw = false;
    try { merkle_root({"zz"}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

static void test_merkle_sensitivity() {
    std::cout << "[merkle: turinio keitimas, tvarkos keitimas, kolizijos paieska]\n";
    std::vector<Transaction> txs;
    for (uint64_t i = 0; i < 5; ++i) txs.push_back(make_tx("aa", "bb", 10 + static_cast<int64_t>(i), i));
    const std::string root = compute_tx_root(txs);
    auto changed = txs;  // pakeistas turinys ir perskaiciuotas ID
    changed[2].amount += 1;
    changed[2].id = compute_tx_id(changed[2]);
    CHECK(compute_tx_root(changed) != root);
    auto swapped = txs;  // sukeista tvarka
    std::swap(swapped[1], swapped[3]);
    CHECK(compute_tx_root(swapped) != root);
    auto swapped2 = txs;
    std::swap(swapped2[0], swapped2[1]);
    CHECK(compute_tx_root(swapped2) != root);
    // Koliziju paieska: 20 000 skirtingu myhash ivesciu ir 3 000 skirtingu Merkle saknu - koliziju nerasta.
    std::set<std::string> hs, roots;
    for (int i = 0; i < 20000; ++i) hs.insert(myhash_hex("coll" + std::to_string(i)));
    CHECK(hs.size() == 20000);
    for (int i = 0; i < 3000; ++i) {
        std::vector<std::string> ids = {myhash_hex("p" + std::to_string(i)), myhash_hex("q" + std::to_string(i)),
                                        myhash_hex("r" + std::to_string(i))};
        roots.insert(merkle_root(ids));
    }
    CHECK(roots.size() == 3000);
}

static Chain test_chain(int64_t a, int64_t b, size_t max_txs = 100, uint32_t diff = 2) {
    std::vector<User> us = {{"A", "aaaa", a}, {"B", "bbbb", b}, {"C", "cccc", 0}};
    ChainParams p;
    p.difficulty = diff;
    p.max_txs = max_txs;
    return make_chain(us, p);
}
static Block mined(const Chain& ch, std::vector<Transaction> txs) {
    Block b = make_candidate_block(ch, std::move(txs), false);
    mine_block(b);
    return b;
}
static bool same_state(const Chain& x, const Chain& y) {
    return x.blocks.size() == y.blocks.size() && x.balances == y.balances && x.ids == y.ids && x.seqs == y.seqs &&
           x.tip().hash == y.tip().hash;
}

static void test_pool_admission() {
    std::cout << "[1 etapas: priemimas i pool]\n";
    Chain ch = test_chain(100, 0);
    Pool pool;
    CHECK(pool.admit(make_tx("aaaa", "bbbb", 70, 0), ch).empty());
    CHECK(!pool.admit(make_tx("zzzz", "bbbb", 1, 1), ch).empty());       // blogas formatas
    CHECK(!pool.admit(make_tx("dddd", "bbbb", 1, 1), ch).empty());       // nezinomas siuntejas
    CHECK(!pool.admit(make_tx("aaaa", "dddd", 1, 1), ch).empty());       // nezinomas gavejas
    CHECK(!pool.admit(make_tx("aaaa", "aaaa", 1, 1), ch).empty());       // sau
    CHECK(!pool.admit(make_tx("aaaa", "bbbb", 0, 1), ch).empty());       // 0
    CHECK(!pool.admit(make_tx("aaaa", "bbbb", -4, 1), ch).empty());      // neigiama
    Transaction bad = make_tx("aaaa", "bbbb", 5, 2);
    bad.amount = 6;                                                      // ID neatitinka
    CHECK(!pool.admit(bad, ch).empty());
    Transaction badfmt = make_tx("aaaa", "bbbb", 5, 3);
    badfmt.id = "XYZ";
    CHECK(!pool.admit(badfmt, ch).empty());
    CHECK(pool.admit(make_tx("aaaa", "bbbb", 70, 0), ch) == "ID jau yra fonde");
    CHECK(!pool.admit(make_tx("bbbb", "cccc", 3, 0), ch).empty());       // sequence jau fonde
    // Pool nerezervuoja lesu: abi 70 ir 60 priimamos, nors kartu 130 > 100.
    CHECK(pool.admit(make_tx("aaaa", "cccc", 60, 1), ch).empty());
    CHECK(pool.size() == 2);
    // ID jau grandineje
    Block b = mined(ch, {make_tx("aaaa", "bbbb", 70, 0)});
    std::string err;
    CHECK(accept_block(ch, b, err));
    CHECK(!pool.admit(make_tx("aaaa", "bbbb", 70, 0), ch).empty());
    Pool p2;
    CHECK(p2.admit(make_tx("aaaa", "bbbb", 70, 0), ch) == "ID jau itrauktas i grandine");
    pool.remove_included(b.txs);
    CHECK(pool.size() == 1);
}

static void test_candidate_sequential() {
    std::cout << "[2 etapas: kandidato atranka, bendras lesu poreikis]\n";
    Chain ch = test_chain(100, 0);
    Transaction t1 = make_tx("aaaa", "bbbb", 70, 0), t2 = make_tx("aaaa", "cccc", 60, 1);
    auto r1 = filter_executable(ch.balances, {t1, t2}, 100);
    CHECK(r1.size() == 1 && r1[0].id == t1.id);  // T1 itraukta, T2 (60 > 30) ne
    auto r2 = filter_executable(ch.balances, {t2, t1}, 100);
    CHECK(r2.size() == 1 && r2[0].id == t2.id);  // atvirkstine tvarka: A lieka 40, T1 netinka
    CHECK(ch.balances["aaaa"] == 100);           // tikrieji balansai nepakito
    auto r3 = filter_executable(ch.balances, {t1, t1}, 100);  // dublikatas viename kandidate
    CHECK(r3.size() == 1);
    // Virsutine riba ir pool atranka be pasikartojimu.
    Chain big = test_chain(1000000, 1000000);
    Pool pool;
    for (uint64_t i = 0; i < 150; ++i) CHECK(pool.admit(make_tx("aaaa", "bbbb", 1, i), big).empty());
    std::mt19937_64 rng(5);
    auto c1 = select_candidate(big.balances, pool, rng, 100), c2 = select_candidate(big.balances, pool, rng, 100);
    CHECK(c1.size() == 100 && c2.size() == 100);
    std::set<std::string> s1, s2;
    for (auto& t : c1) s1.insert(t.id);
    for (auto& t : c2) s2.insert(t.id);
    CHECK(s1.size() == 100 && s2.size() == 100);  // viename kandidate nesikartoja
    CHECK(s1 != s2);                              // kandidatai skiriasi (ir gali persidengti)
    size_t common = 0;
    for (auto& id : s1) common += s2.count(id);
    CHECK(common > 0);
}

static void test_block_acceptance() {
    std::cout << "[3 etapas: bloko priemimas ir atmetimas]\n";
    Transaction t1 = make_tx("aaaa", "bbbb", 70, 0), t2 = make_tx("aaaa", "cccc", 70, 1);
    std::string err;

    {  // du mokejimai po 70, A turi 100: abu kartu negalimi
        Chain ch = test_chain(100, 0);
        Chain before = ch;
        Block both = mined(ch, {t1, t2});
        CHECK(!accept_block(ch, both, err));
        CHECK(same_state(ch, before));  // netinkamas blokas nieko nepakeite
        Block one = mined(ch, {t1});
        CHECK(accept_block(ch, one, err));
        CHECK(ch.balances["aaaa"] == 30 && ch.balances["bbbb"] == 70);
        CHECK(ch.ids.count(t1.id) == 1 && ch.blocks.size() == 2);
        CHECK(total_supply(ch.balances) == 100);
    }
    {  // perpildymas
        Chain ch = test_chain(100, INT64_MAX - 5);
        Transaction o = make_tx("aaaa", "bbbb", 10, 0);
        Balances tmp = ch.balances;
        CHECK(apply_tx(o, tmp) == ApplyStatus::Overflow);
        Chain before = ch;
        CHECK(!accept_block(ch, mined(ch, {o}), err));
        CHECK(same_state(ch, before));
    }
    // Netinkami blokai: kiekvienas turi buti atmestas ir nekeisti grandines.
    Chain ch = test_chain(1000, 0);
    const Chain snapshot = ch;
    auto expect_reject = [&](Block b, const char* what) {
        std::string e;
        ++g_checks;
        bool ok = accept_block(ch, b, e);
        if (ok || e.empty() || !same_state(ch, snapshot)) {
            ++g_failures;
            std::cerr << "  nepavyko atmetimas: " << what << "\n";
        }
    };
    Block good = mined(ch, {t1});
    {
        Block b = good;
        b.header.prev_hash = std::string(64, '1');
        mine_block(b);
        expect_reject(b, "blogas prev_hash");
    }
    {
        Block b = good;
        b.header.version = 3;
        mine_block(b);
        expect_reject(b, "nepalaikoma versija");
    }
    {
        Block b = good;
        b.header.tx_root = myhash_hex("x");
        mine_block(b);
        expect_reject(b, "bloga tx_root");
    }
    {
        Block b = good;
        b.header.difficulty = 1;  // kandidatas bando sumazinti sudetinguma
        mine_block(b);
        expect_reject(b, "sumazintas difficulty");
    }
    {
        Block b = good;  // PoW netenkinamas, nors hash vidiskai teisingas
        for (uint64_t n = 0;; ++n) {
            b.header.nonce = n;
            b.hash = compute_header_hash(b.header);
            if (!meets_difficulty(b.hash, 2)) break;
        }
        expect_reject(b, "PoW netenkinamas");
    }
    {
        Block b = good;
        b.hash = std::string(64, '0');  // suklastotas hash
        expect_reject(b, "suklastotas hash");
    }
    {
        Block b = good;
        b.txs[0].amount = 71;  // pakeistas turinys be ID perskaiciavimo
        expect_reject(b, "pakeista suma");
    }
    expect_reject(mined(ch, {t1, t1}), "ID bloke pasikartoja");
    expect_reject(Block(make_genesis()), "genesis kaip naujas blokas");
    {
        Block b = mined(ch, {});  // tuscias blokas
        expect_reject(b, "tuscias blokas");
    }
    {  // per daug transakciju
        Chain small = test_chain(1000, 0, 2);
        const Chain snap2 = small;
        Block b = mined(small, {make_tx("aaaa", "bbbb", 1, 0), make_tx("aaaa", "bbbb", 1, 1), make_tx("aaaa", "bbbb", 1, 2)});
        CHECK(!accept_block(small, b, err));
        CHECK(same_state(small, snap2));
    }
    {  // ID jau grandineje (pakartotinis itraukimas)
        Chain c2 = test_chain(1000, 0);
        CHECK(accept_block(c2, mined(c2, {t1}), err));
        Chain before = c2;
        CHECK(!accept_block(c2, mined(c2, {t1}), err));
        CHECK(same_state(c2, before));
    }
    {  // Merkle dvilypumas: [a,b,c] ir [a,b,c,c] turi ta pacia sakni, bet dublikatas atmetamas
        Chain c3 = test_chain(100000, 0);
        std::vector<Transaction> abc = {make_tx("aaaa", "bbbb", 1, 0), make_tx("aaaa", "bbbb", 2, 1), make_tx("aaaa", "cccc", 3, 2)};
        auto abcc = abc;
        abcc.push_back(abc[2]);
        CHECK(compute_tx_root(abc) == compute_tx_root(abcc));  // zinoma Merkle savybe (README)
        Chain before = c3;
        CHECK(!accept_block(c3, mined(c3, abcc), err));        // apsaugo dubliuotu ID draudimas
        CHECK(same_state(c3, before));
        CHECK(accept_block(c3, mined(c3, abc), err));
    }
}

static void test_competition() {
    std::cout << "[5 kandidatu lenktynes]\n";
    Config c = small_cfg();
    c.seed = 21;
    c.initial_budget = 20;  // mazas biudzetas -> prireiks keliu raundu
    auto us = generate_users(80, 21);
    auto txs = generate_transactions(us, 700, 21, true);
    CompResult r = run_competition(us, txs, c, nullptr);
    std::string err;
    CHECK(validate_chain(r.chain.blocks, r.chain.initial, c.difficulty, err, c.block_size));
    CHECK(r.supply_before == r.supply_after);
    for (auto& kv : r.chain.balances) CHECK(kv.second >= 0);
    CHECK(r.included + r.rejected.size() + r.remaining.size() == r.total_input);
    CHECK(r.rejected.size() == 8);  // tiksliai 8 demonstracines netinkamos
    CHECK(r.races.size() == r.chain.blocks.size() - 1);
    bool multi = false;
    for (const auto& race : r.races) {
        CHECK(race.cands.size() == 5);
        CHECK(race.winner >= 0);
        const auto& w = race.cands[static_cast<size_t>(race.winner)];
        CHECK(w.solved && w.attempts == w.nonce + 1);  // nonce tesiamas nuo netikrintu: be pasikartojimu
        for (const auto& k : race.cands) {
            CHECK(k.ntx >= 1 && k.ntx <= c.block_size);
            if (k.solved) CHECK(w.attempts <= k.attempts);  // laimetojas - maziausiai bandymu
        }
        if (race.rounds > 1) multi = true;
    }
    CHECK(multi);  // bent karta biudzetas buvo didinamas
    // Determinizmas
    CompResult r2 = run_competition(us, txs, c, nullptr);
    CHECK(r.chain.blocks.size() == r2.chain.blocks.size());
    for (size_t i = 0; i < r.chain.blocks.size() && i < r2.chain.blocks.size(); ++i)
        CHECK(r.chain.blocks[i].hash == r2.chain.blocks[i].hash);
}

static void test_competition_limits() {
    std::cout << "[kandidatu ribos ir sustabdymas]\n";
    std::vector<User> us = {{"A", "aaaa", 100}, {"B", "bbbb", 0}, {"C", "cccc", 0}};
    // 1) bandymu riba: sudetingumas 6, tik 50 bandymu -> aiskus sustabdymas, grandine nesikeicia, difficulty nemazinamas
    Config c = small_cfg(6);
    c.max_attempts = 50;
    c.initial_budget = 10;
    CompResult r = run_competition(us, {make_tx("aaaa", "bbbb", 5, 0), make_tx("aaaa", "cccc", 5, 1)}, c, nullptr);
    CHECK(!r.stop_reason.empty());
    CHECK(r.chain.blocks.size() == 1 && r.included == 0);
    CHECK(r.remaining.size() == 2);
    CHECK(r.chain.balances["aaaa"] == 100);
    for (const auto& race : r.races)
        for (const auto& k : race.cands) CHECK(k.attempts <= 50 && !k.solved);
    // 2) pazangos nera: T1 70 + T2 60 su A=100 -> itraukiama viena, kita lieka su priezastimi
    Config c2 = small_cfg();
    CompResult r2 = run_competition(us, {make_tx("aaaa", "bbbb", 70, 0), make_tx("aaaa", "cccc", 60, 1)}, c2, nullptr);
    CHECK(r2.included == 1 && r2.remaining.size() == 1);
    CHECK(r2.remaining[0].reason.find("nepakanka lesu") != std::string::npos);
    CHECK(!r2.stop_reason.empty());
    CHECK(r2.supply_after == 100);
}


int main() {
    test_myhash();
    test_serialization();
    test_genesis_and_header();
    test_hand_case();
    test_no_overspend_in_block();
    test_partial_last_block();
    test_determinism();
    test_tampering();
    test_full_run_invariants();
    test_myhash_extra();
    test_merkle_cases();
    test_merkle_sensitivity();
    test_pool_admission();
    test_candidate_sequential();
    test_block_acceptance();
    test_competition();
    test_competition_limits();
    std::cout << "\npatikrinimu: " << g_checks << ", nepavyko: " << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}