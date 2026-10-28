#include <iostream>
#include <set>

#include "blockchain.h"
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
    std::cout << "\npatikrinimu: " << g_checks << ", nepavyko: " << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}