// v0.3: kandidatu lenktynes. Kasimo ciklas naudoja optimizuota mine_range
// (perkelta i blockchain.cpp); kitu pakeitimu nera — semantika tokia pati kaip v0.2.
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "blockchain.h"
#include "myhash.h"

namespace bc {

namespace {
std::string shorten(const std::string& h, bool full) { return full ? h : h.substr(0, 16) + ".."; }
struct Cand {
    int index = 0;
    Block block;
    uint64_t next_nonce = 0;
    uint64_t attempts = 0;
    double ms = 0;
    bool solved = false;
};
}  // namespace

Chain make_chain(const std::vector<User>& users, const ChainParams& params) {
    Chain c;
    c.params = params;
    for (const auto& u : users) c.initial[u.public_key] = u.balance;
    c.balances = c.initial;
    c.blocks.push_back(make_genesis(params.version));
    return c;
}

std::string Pool::admit(const Transaction& tx, const Chain& chain) {
    if (!is_lower_hex(tx.sender) || !is_lower_hex(tx.receiver))
        return "neteisingas dalyvio identifikatoriaus formatas";
    if (!is_valid_tx_id(tx.id)) return "neteisingas transakcijos ID formatas";
    std::string why = check_tx_structure(tx, chain.balances);
    if (!why.empty()) return why;
    if (chain.ids.count(tx.id)) return "ID jau itrauktas i grandine";
    if (ids_.count(tx.id)) return "ID jau yra fonde";
    if (chain.seqs.count(tx.sequence)) return "sequence jau panaudotas grandineje";
    if (seqs_.count(tx.sequence)) return "sequence jau yra fonde";
    txs_.push_back(tx);
    ids_.insert(tx.id);
    seqs_.insert(tx.sequence);
    return "";
}

void Pool::remove_included(const std::vector<Transaction>& included) {
    std::unordered_set<std::string> rm;
    for (const auto& t : included) rm.insert(t.id);
    std::vector<Transaction> keep;
    keep.reserve(txs_.size());
    for (auto& t : txs_) {
        if (rm.count(t.id)) { ids_.erase(t.id); seqs_.erase(t.sequence); }
        else keep.push_back(std::move(t));
    }
    txs_ = std::move(keep);
}

std::vector<Transaction> filter_executable(const Balances& committed,
                                           const std::vector<Transaction>& ordered, size_t max_txs) {
    Balances tmp = committed;
    std::vector<Transaction> out;
    std::unordered_set<std::string> local;
    for (const auto& tx : ordered) {
        if (out.size() >= max_txs) break;
        if (local.count(tx.id)) continue;
        if (apply_tx(tx, tmp) == ApplyStatus::Ok) { out.push_back(tx); local.insert(tx.id); }
    }
    return out;
}

std::vector<Transaction> select_candidate(const Balances& committed, const Pool& pool,
                                          std::mt19937_64& rng, size_t max_txs) {
    const auto& all = pool.txs();
    std::vector<size_t> idx(all.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    for (size_t i = idx.size(); i > 1; --i) std::swap(idx[i - 1], idx[rng() % i]);
    Balances tmp = committed;
    std::vector<Transaction> out;
    for (size_t i : idx) {
        if (out.size() >= max_txs) break;
        if (apply_tx(all[i], tmp) == ApplyStatus::Ok) out.push_back(all[i]);
    }
    return out;
}

Block make_candidate_block(const Chain& chain, std::vector<Transaction> txs, bool real_time) {
    Block b;
    b.height = chain.blocks.size();
    b.txs = std::move(txs);
    b.header.prev_hash = chain.tip().hash;
    b.header.version = chain.params.version;
    b.header.difficulty = chain.params.difficulty;
    b.header.tx_root = compute_tx_root(b.txs, b.header.version);
    int64_t ts = real_time ? static_cast<int64_t>(std::time(nullptr))
                           : kGenesisTimestamp + static_cast<int64_t>(b.height) * kBlockInterval;
    b.header.timestamp = std::max(ts, chain.tip().header.timestamp);
    return b;
}

bool accept_block(Chain& chain, const Block& b, std::string& err) {
    if (b.header.version != chain.params.version) { err = "versija nesutampa su grandines"; return false; }
    if (!verify_block(b, chain.tip(), chain.balances, chain.ids, chain.params.difficulty, err,
                      chain.params.max_txs, chain.seqs))
        return false;
    Balances nb = chain.balances;
    for (const auto& tx : b.txs)
        if (apply_tx(tx, nb) != ApplyStatus::Ok) { err = "netaikoma busenai"; return false; }
    chain.balances = std::move(nb);
    for (const auto& tx : b.txs) { chain.ids.insert(tx.id); chain.seqs.insert(tx.sequence); }
    chain.blocks.push_back(b);
    return true;
}

CompResult run_competition(const std::vector<User>& users, const std::vector<Transaction>& input,
                           const Config& cfg, std::ostream* out) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    ChainParams params;
    params.version = cfg.version;
    params.difficulty = cfg.difficulty;
    params.max_txs = cfg.block_size;

    CompResult res;
    res.chain = make_chain(users, params);
    res.supply_before = total_supply(res.chain.balances);
    res.total_input = input.size();

    Pool pool;
    for (const auto& tx : input) {
        std::string why = pool.admit(tx, res.chain);
        if (!why.empty()) res.rejected.push_back({tx, why});
    }

    std::mt19937_64 rng(cfg.seed ^ 0xC0FFEEULL);

    while (!pool.empty() && (cfg.max_blocks == 0 || res.chain.blocks.size() - 1 < cfg.max_blocks)) {
        std::vector<Cand> cands;
        for (size_t k = 0; k < cfg.num_candidates; ++k) {
            auto txs = select_candidate(res.chain.balances, pool, rng, cfg.block_size);
            if (txs.empty()) break;
            Cand c;
            c.index = static_cast<int>(k) + 1;
            c.block = make_candidate_block(res.chain, std::move(txs), cfg.real_time);
            cands.push_back(std::move(c));
        }
        if (cands.empty()) {
            res.stop_reason = "pazangos nera: fonde nebera transakciju, vykdomu pagal dabartine busena";
            break;
        }

        BlockRace race;
        race.height = res.chain.blocks.size();
        {
            std::unordered_set<std::string> d;
            for (const auto& c : cands) for (const auto& tx : c.block.txs) d.insert(tx.id);
            race.distinct_txs = d.size();
        }

        uint64_t budget = cfg.initial_budget;
        int winner = -1, round = 0;
        auto exhausted = [&](const Cand& c) {
            return c.attempts >= cfg.max_attempts || c.ms / 1000.0 >= cfg.time_limit_s;
        };
        for (;;) {
            ++round;
            bool any_active = false;
            for (auto& c : cands) {
                if (c.solved || exhausted(c)) continue;
                any_active = true;
                const uint64_t allowed = std::min<uint64_t>(budget, cfg.max_attempts - c.attempts);
                uint64_t used = 0;
                const auto m0 = clock::now();
                bool ok = mine_range(c.block, c.next_nonce, allowed, used);   // v0.3 optimised
                c.ms += std::chrono::duration<double, std::milli>(clock::now() - m0).count();
                c.attempts += used;
                if (ok) c.solved = true;
            }
            for (size_t i = 0; i < cands.size(); ++i)
                if (cands[i].solved && (winner < 0 || cands[i].attempts < cands[static_cast<size_t>(winner)].attempts))
                    winner = static_cast<int>(i);
            if (winner >= 0 || !any_active) break;
            budget = std::min<uint64_t>(budget * cfg.budget_growth, cfg.max_attempts);
        }
        race.rounds = round;
        race.winner = winner;
        for (const auto& c : cands) {
            CandidateReport r;
            r.index = c.index;
            r.ntx = c.block.txs.size();
            r.attempts = c.attempts;
            r.ms = c.ms;
            r.solved = c.solved;
            r.nonce = c.block.header.nonce;
            r.hash = c.solved ? c.block.hash : "";
            race.cands.push_back(r);
            res.total_attempts += c.attempts;
        }

        if (winner < 0) {
            res.races.push_back(race);
            res.stop_reason = "kasimo riba pasiekta aukstyje " + std::to_string(race.height) +
                              ": joks kandidatas nerado nonce (sudetingumas nemazinamas)";
            if (out) *out << "!! " << res.stop_reason << "\n";
            break;
        }

        const Block& w = cands[static_cast<size_t>(winner)].block;
        std::string err;
        if (!accept_block(res.chain, w, err))
            throw std::runtime_error("laimetojo blokas nepraejo patikros: " + err);
        pool.remove_included(w.txs);
        res.included += w.txs.size();

        if (out && cfg.verbose) {
            const auto& wr = race.cands[static_cast<size_t>(winner)];
            *out << "Blokas #" << std::setw(3) << w.height << " | laimejo K" << wr.index << " | tx=" << std::setw(3)
                 << w.txs.size() << " | prev=" << shorten(w.header.prev_hash, cfg.full_hash)
                 << " | root=" << shorten(w.header.tx_root, cfg.full_hash)
                 << " | hash=" << shorten(w.hash, cfg.full_hash) << " | diff=" << w.header.difficulty
                 << " | nonce=" << w.header.nonce << " | raundai=" << race.rounds << "\n";
            *out << "   kandidatai:";
            for (const auto& r : race.cands) {
                *out << " K" << r.index << "[" << r.ntx << "tx, " << r.attempts << " band., " << std::fixed
                     << std::setprecision(1) << r.ms << " ms, " << (r.solved ? "rasta" : "nerasta") << "]";
            }
            *out << "\n";
        }
        res.races.push_back(std::move(race));
    }

    for (const auto& tx : pool.txs()) {
        std::string reason;
        auto it = res.chain.balances.find(tx.sender);
        if (it != res.chain.balances.end() && it->second < tx.amount)
            reason = "nepakanka lesu (siuntejas turi " + std::to_string(it->second) + ", reikia " +
                     std::to_string(tx.amount) + "); pagal esama busena vykdymas neimanomas";
        else
            reason = "nenagrineta (sustabdyta: " + (res.stop_reason.empty() ? std::string("bloku limitas") : res.stop_reason) + ")";
        res.remaining.push_back({tx, reason});
    }

    res.supply_after = total_supply(res.chain.balances);
    if (res.supply_after != res.supply_before) throw std::runtime_error("bendra lesu suma pasikeite");
    res.seconds = std::chrono::duration<double>(clock::now() - t0).count();
    return res;
}

}  // namespace bc