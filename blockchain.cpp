#include "blockchain.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>

#include "myhash.h"

namespace bc {

std::string serialize_tx(const std::string& sender, const std::string& receiver, int64_t amount,
                         uint64_t sequence) {
    return sender + "|" + receiver + "|" + std::to_string(amount) + "|" + std::to_string(sequence);
}

std::string compute_tx_id(const Transaction& tx) {
    return myhash_hex(serialize_tx(tx.sender, tx.receiver, tx.amount, tx.sequence));
}

Transaction make_tx(const std::string& sender, const std::string& receiver, int64_t amount,
                    uint64_t sequence) {
    Transaction tx;
    tx.sender = sender;
    tx.receiver = receiver;
    tx.amount = amount;
    tx.sequence = sequence;
    tx.id = compute_tx_id(tx);
    return tx;
}

std::string compute_tx_root(const std::vector<Transaction>& txs) {
    std::string cat;
    cat.reserve(txs.size() * 64);
    for (const auto& tx : txs) cat += tx.id;
    return myhash_hex(cat);
}

std::string serialize_header(const BlockHeader& h) {
    return h.prev_hash + "|" + std::to_string(h.timestamp) + "|" + std::to_string(h.version) + "|" +
           h.tx_root + "|" + std::to_string(h.nonce) + "|" + std::to_string(h.difficulty);
}

std::string compute_header_hash(const BlockHeader& h) { return myhash_hex(serialize_header(h)); }

bool meets_difficulty(const std::string& hash, uint32_t difficulty) {
    if (hash.size() < difficulty) return false;
    for (uint32_t i = 0; i < difficulty; ++i)
        if (hash[i] != '0') return false;
    return true;
}

Block make_genesis() {
    Block g;
    g.height = 0;
    g.header.prev_hash = std::string(64, '0');
    g.header.timestamp = kGenesisTimestamp;
    g.header.version = kVersion;
    g.header.tx_root = myhash_hex("");
    g.header.nonce = 0;
    g.header.difficulty = 0;
    g.hash = compute_header_hash(g.header);
    return g;
}

std::string check_tx_structure(const Transaction& tx, const Balances& accounts) {
    if (accounts.find(tx.sender) == accounts.end()) return "nezinomas siuntejas";
    if (accounts.find(tx.receiver) == accounts.end()) return "nezinomas gavejas";
    if (tx.sender == tx.receiver) return "siuntejas ir gavejas sutampa";
    if (tx.amount <= 0) return "suma nera teigiama";
    if (tx.id != compute_tx_id(tx)) return "transakcijos id neatitinka turinio";
    return "";
}

ApplyStatus apply_tx(const Transaction& tx, Balances& state) {
    auto s = state.find(tx.sender);
    auto r = state.find(tx.receiver);
    if (s == state.end() || r == state.end() || s == r) return ApplyStatus::UnknownAccount;
    if (tx.amount <= 0) return ApplyStatus::InvalidAmount;
    if (s->second < tx.amount) return ApplyStatus::InsufficientFunds;
    if (r->second > INT64_MAX - tx.amount) return ApplyStatus::Overflow;
    s->second -= tx.amount;
    r->second += tx.amount;
    return ApplyStatus::Ok;
}

int64_t total_supply(const Balances& state) {
    int64_t sum = 0;
    for (const auto& kv : state) sum += kv.second;
    return sum;
}

bool verify_block(const Block& b, const Block& prev, const Balances& state,
                  const std::unordered_set<std::string>& seen_ids, uint32_t expected_difficulty,
                  std::string& err) {
    if (b.height != prev.height + 1) { err = "neteisingas aukstis"; return false; }
    if (b.header.version != kVersion) { err = "nepalaikoma versija"; return false; }
    if (compute_header_hash(prev.header) != prev.hash) {
        err = "ankstesnio bloko hash neatitinka jo antraštes";
        return false;
    }
    if (b.header.prev_hash != prev.hash) { err = "prev_hash nesutampa su ankstesnio bloko hash"; return false; }
    if (b.header.timestamp < prev.header.timestamp) { err = "laiko zyma mazesne nei ankstesnio bloko"; return false; }
    if (b.txs.empty()) { err = "tuscias blokas"; return false; }
    if (b.header.difficulty != expected_difficulty) { err = "netinkamas difficulty"; return false; }
    if (compute_tx_root(b.txs) != b.header.tx_root) { err = "tx_root neatitinka transakciju"; return false; }

    const std::string h = compute_header_hash(b.header);
    if (h != b.hash) { err = "deklaruotas hash neatitinka antraštes"; return false; }
    if (!meets_difficulty(h, b.header.difficulty)) { err = "hash netenkina pow salygos"; return false; }

    Balances tmp = state;
    std::unordered_set<std::string> local;
    for (size_t i = 0; i < b.txs.size(); ++i) {
        const Transaction& tx = b.txs[i];
        std::string why = check_tx_structure(tx, tmp);
        if (!why.empty()) { err = "tx " + std::to_string(i) + ": " + why; return false; }
        if (seen_ids.count(tx.id) || !local.insert(tx.id).second) {
            err = "tx " + std::to_string(i) + ": dubliuota transakcija";
            return false;
        }
        if (apply_tx(tx, tmp) != ApplyStatus::Ok) {
            err = "tx " + std::to_string(i) + ": nepakanka lesu arba netinkamas taikymas";
            return false;
        }
    }
    return true;
}

bool validate_chain(const std::vector<Block>& chain, const Balances& initial,
                    uint32_t expected_difficulty, std::string& err) {
    if (chain.empty()) { err = "tuscia grandine"; return false; }
    const Block g = make_genesis();
    if (chain[0].hash != g.hash || chain[0].header.prev_hash != g.header.prev_hash ||
        chain[0].header.tx_root != g.header.tx_root || !chain[0].txs.empty() || chain[0].height != 0) {
        err = "genesis blokas neatitinka apibrezimo";
        return false;
    }
    Balances state = initial;
    const int64_t supply = total_supply(initial);
    std::unordered_set<std::string> seen;
    for (size_t i = 1; i < chain.size(); ++i) {
        std::string e;
        if (!verify_block(chain[i], chain[i - 1], state, seen, expected_difficulty, e)) {
            err = "blokas #" + std::to_string(i) + ": " + e;
            return false;
        }
        for (const auto& tx : chain[i].txs) {
            apply_tx(tx, state);
            seen.insert(tx.id);
        }
    }
    for (const auto& kv : state)
        if (kv.second < 0) { err = "neigiamas balansas"; return false; }
    if (total_supply(state) != supply) { err = "bendra lesu suma pasikeite"; return false; }
    return true;
}

uint64_t mine_block(Block& b) {
    uint64_t attempts = 0;
    b.header.nonce = 0;
    for (;;) {
        ++attempts;
        std::string h = compute_header_hash(b.header);
        if (meets_difficulty(h, b.header.difficulty)) {
            b.hash = h;
            return attempts;
        }
        ++b.header.nonce;
    }
}

std::vector<User> generate_users(size_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::unordered_set<std::string> keys;
    std::vector<User> users;
    users.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        User u;
        std::ostringstream nm;
        nm << "User" << std::setw(4) << std::setfill('0') << (i + 1);
        u.name = nm.str();
        u.public_key = myhash_hex("user|" + std::to_string(seed) + "|" + std::to_string(i)).substr(0, 32);
        if (!keys.insert(u.public_key).second) throw std::runtime_error("public_key kolizija");
        u.balance = 100 + static_cast<int64_t>(rng() % 999901ULL);
        users.push_back(u);
    }
    return users;
}

std::vector<Transaction> generate_transactions(const std::vector<User>& users, size_t n,
                                               uint64_t seed, bool inject_invalid) {
    if (users.size() < 2) throw std::runtime_error("reikia bent 2 vartotoju");
    std::mt19937_64 rng(seed ^ 0x7478ULL);
    const uint64_t U = users.size();
    std::vector<Transaction> txs;
    txs.reserve(n + 8);
    for (size_t i = 0; i < n; ++i) {
        uint64_t si = rng() % U;
        uint64_t ri = rng() % (U - 1);
        if (ri >= si) ++ri;
        const int64_t bal = users[si].balance;
        int64_t amount;
        if (rng() % 50 == 0)
            amount = 1 + static_cast<int64_t>(rng() % static_cast<uint64_t>(2 * bal));
        else
            amount = 1 + static_cast<int64_t>(rng() % static_cast<uint64_t>(std::max<int64_t>(1, bal / 10)));
        txs.push_back(make_tx(users[si].public_key, users[ri].public_key, amount, i));
    }
    if (inject_invalid) {
        uint64_t seq = n;
        const std::string& a = users[0].public_key;
        const std::string& b = users[1].public_key;
        txs.push_back(make_tx(std::string(32, 'f'), a, 10, seq++));
        txs.push_back(make_tx(a, std::string(32, 'e'), 10, seq++));
        txs.push_back(make_tx(a, a, 10, seq++));
        txs.push_back(make_tx(a, b, 0, seq++));
        txs.push_back(make_tx(a, b, -5, seq++));
        Transaction bad_id = make_tx(a, b, 1, seq++);
        bad_id.amount = 2;
        txs.push_back(bad_id);
        txs.push_back(txs[0]);
        txs.push_back(make_tx(b, a, 7, 0));
    }
    return txs;
}

namespace {
std::string shorten(const std::string& h, bool full) { return full ? h : h.substr(0, 16) + ".."; }

struct Pending {
    Transaction tx;
    int defers = 0;
};
}

SimResult run_simulation(const std::vector<User>& users, const std::vector<Transaction>& input,
                         const Config& cfg, std::ostream* out) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    SimResult res;
    for (const auto& u : users) res.initial[u.public_key] = u.balance;
    Balances state = res.initial;
    res.supply_before = total_supply(state);
    res.total_input = input.size();
    res.chain.push_back(make_genesis());

    std::vector<Pending> pool;
    std::unordered_set<std::string> pool_ids;
    std::unordered_set<uint64_t> pool_seq;
    for (const auto& tx : input) {
        std::string why = check_tx_structure(tx, state);
        if (why.empty() && pool_ids.count(tx.id)) why = "dubliuota transakcija (toks pat id)";
        if (why.empty() && pool_seq.count(tx.sequence)) why = "pasikartojantis sequence";
        if (!why.empty()) {
            res.rejected.push_back({tx, why});
            continue;
        }
        pool_ids.insert(tx.id);
        pool_seq.insert(tx.sequence);
        pool.push_back({tx, 0});
    }

    std::mt19937_64 rng(cfg.seed ^ 0xB10C0001ULL);
    std::unordered_set<std::string> chain_ids;

    while (!pool.empty() && (cfg.max_blocks == 0 || res.chain.size() - 1 < cfg.max_blocks)) {
        const size_t k = std::min(cfg.block_size, pool.size());
        for (size_t i = 0; i < k; ++i) {
            size_t j = i + static_cast<size_t>(rng() % (pool.size() - i));
            std::swap(pool[i], pool[j]);
        }
        std::vector<Pending> cand(std::make_move_iterator(pool.begin()),
                                  std::make_move_iterator(pool.begin() + k));
        pool.erase(pool.begin(), pool.begin() + k);

        Balances temp = state;
        std::vector<Transaction> included;
        for (auto& p : cand) {
            ApplyStatus st = apply_tx(p.tx, temp);
            if (st == ApplyStatus::Ok) {
                included.push_back(p.tx);
            } else if (st == ApplyStatus::InsufficientFunds) {
                if (++p.defers >= cfg.max_defers)
                    res.rejected.push_back(
                        {p.tx, "nepakanka lesu (atmesta po " + std::to_string(p.defers) + " bandymu)"});
                else
                    pool.push_back(std::move(p));
            } else {
                res.rejected.push_back({p.tx, "netaikoma busenai (perpildymas arba netinkama saskaita)"});
            }
        }
        if (included.empty()) continue;

        Block b;
        b.height = res.chain.size();
        b.txs = included;
        b.header.prev_hash = res.chain.back().hash;
        b.header.version = kVersion;
        b.header.difficulty = cfg.difficulty;
        b.header.tx_root = compute_tx_root(b.txs);
        int64_t ts = cfg.real_time ? static_cast<int64_t>(std::time(nullptr))
                                   : kGenesisTimestamp + static_cast<int64_t>(b.height) * kBlockInterval;
        b.header.timestamp = std::max(ts, res.chain.back().header.timestamp);

        const auto m0 = clock::now();
        const uint64_t attempts = mine_block(b);
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - m0).count();

        std::string err;
        if (!verify_block(b, res.chain.back(), state, chain_ids, cfg.difficulty, err))
            throw std::runtime_error("iskastas blokas nepraėjo patikros: " + err);
        for (const auto& tx : b.txs) {
            apply_tx(tx, state);
            chain_ids.insert(tx.id);
        }
        res.included += b.txs.size();
        res.total_attempts += attempts;
        res.stats.push_back({attempts, ms});

        if (out && cfg.verbose) {
            *out << "blokas #" << std::setw(3) << b.height << " | tx=" << std::setw(3) << b.txs.size()
                 << " | prev=" << shorten(b.header.prev_hash, cfg.full_hash)
                 << " | root=" << shorten(b.header.tx_root, cfg.full_hash)
                 << " | hash=" << shorten(b.hash, cfg.full_hash) << " | diff=" << b.header.difficulty
                 << " | nonce=" << b.header.nonce << " | bandymai=" << attempts << " | " << std::fixed
                 << std::setprecision(1) << ms << " ms\n";
        }
        res.chain.push_back(std::move(b));
    }

    for (auto& p : pool) res.remaining.push_back({p.tx, "nepasiektas bloku limitas (nenagrineta)"});

    res.final_state = state;
    res.supply_after = total_supply(state);
    if (res.supply_after != res.supply_before) throw std::runtime_error("bendra lesu suma pasikeite");
    res.seconds = std::chrono::duration<double>(clock::now() - t0).count();
    return res;
}

}  // namespace bc