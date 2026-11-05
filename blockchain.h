#pragma once
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bc {

constexpr uint32_t kVersionLegacy = 1;
constexpr uint32_t kVersionMerkle = 2;
constexpr uint32_t kVersion = kVersionMerkle;
bool is_supported_version(uint32_t v);
constexpr int64_t kGenesisTimestamp = 1761696000;
constexpr int64_t kBlockInterval = 600;

using Balances = std::unordered_map<std::string, int64_t>;

struct User { std::string name; std::string public_key; int64_t balance = 0; };

struct Transaction {
    std::string sender;
    std::string receiver;
    int64_t amount = 0;
    uint64_t sequence = 0;
    std::string id;
};

struct BlockHeader {
    std::string prev_hash;
    int64_t timestamp = 0;
    uint32_t version = kVersion;
    std::string tx_root;
    uint64_t nonce = 0;
    uint32_t difficulty = 0;
};

struct Block {
    uint64_t height = 0;
    BlockHeader header;
    std::vector<Transaction> txs;
    std::string hash;
};

enum class ApplyStatus { Ok, UnknownAccount, InvalidAmount, InsufficientFunds, Overflow };

std::string serialize_tx(const std::string& sender, const std::string& receiver, int64_t amount,
                         uint64_t sequence);
std::string compute_tx_id(const Transaction& tx);
Transaction make_tx(const std::string& sender, const std::string& receiver, int64_t amount,
                    uint64_t sequence);
bool is_lower_hex(const std::string& s);
bool is_valid_tx_id(const std::string& s);
std::string compute_tx_root(const std::vector<Transaction>& txs, uint32_t version = kVersion);
std::string serialize_header(const BlockHeader& h);
std::string compute_header_hash(const BlockHeader& h);
bool meets_difficulty(const std::string& hash, uint32_t difficulty);
Block make_genesis(uint32_t version = kVersion);

std::string check_tx_structure(const Transaction& tx, const Balances& accounts);
ApplyStatus apply_tx(const Transaction& tx, Balances& state);
int64_t total_supply(const Balances& state);

// v0.3 (AI): extra parameter `seen_seqs` — chain-level sequence set.
// Defaults to empty so v0.2 callers keep working; explicit callers get
// chain-level sequence-uniqueness enforcement.
bool verify_block(const Block& b, const Block& prev, const Balances& state,
                  const std::unordered_set<std::string>& seen_ids,
                  uint32_t expected_difficulty,
                  std::string& err, size_t max_txs = SIZE_MAX,
                  const std::unordered_set<uint64_t>& seen_seqs = {});

bool validate_chain(const std::vector<Block>& chain, const Balances& initial,
                    uint32_t expected_difficulty, std::string& err, size_t max_txs = SIZE_MAX);

uint64_t mine_block(Block& b);                                   // v0.3: optimised inside
bool     mine_range(Block& b, uint64_t& next_nonce,
                    uint64_t budget, uint64_t& used);            // v0.3: moved here + optimised

// v0.3 (AI): benchmark primitive — run both serialisations on the same
// fixed header for `budget` nonces, return both wall times.
struct BenchResult { double old_ms = 0; double new_ms = 0; uint64_t attempts = 0; };
BenchResult bench_mining(const BlockHeader& header, uint64_t budget);

std::vector<User> generate_users(size_t n, uint64_t seed);
std::vector<Transaction> generate_transactions(const std::vector<User>& users, size_t n,
                                               uint64_t seed, bool inject_invalid);

struct Config {
    size_t num_users = 1000;
    size_t num_txs = 10000;
    size_t block_size = 100;
    uint64_t seed = 42;
    uint32_t difficulty = 3;
    int max_defers = 3;
    size_t max_blocks = 0;
    bool inject_invalid = true;
    bool real_time = false;
    bool full_hash = false;
    bool verbose = true;
    uint32_t version = kVersion;
    size_t num_candidates = 5;
    uint64_t initial_budget = 2000;
    uint64_t budget_growth = 2;
    uint64_t max_attempts = 2000000;
    double time_limit_s = 5.0;
    uint64_t bench_mine = 0;      // v0.3: >0 -> run benchmark and exit
};

struct BlockStats { uint64_t attempts = 0; double ms = 0; };
struct RejectedTx { Transaction tx; std::string reason; };

struct SimResult {
    std::vector<Block> chain;
    std::vector<BlockStats> stats;
    Balances initial, final_state;
    std::vector<RejectedTx> rejected, remaining;
    size_t total_input = 0, included = 0;
    int64_t supply_before = 0, supply_after = 0;
    uint64_t total_attempts = 0;
    double seconds = 0;
};

SimResult run_simulation(const std::vector<User>& users, const std::vector<Transaction>& input,
                         const Config& cfg, std::ostream* out);

struct ChainParams { uint32_t version = kVersion; uint32_t difficulty = 3; size_t max_txs = 100; };

struct Chain {
    ChainParams params;
    std::vector<Block> blocks;
    Balances initial;
    Balances balances;
    std::unordered_set<std::string> ids;
    std::unordered_set<uint64_t> seqs;
    const Block& tip() const { return blocks.back(); }
};
Chain make_chain(const std::vector<User>& users, const ChainParams& params);

class Pool {
public:
    std::string admit(const Transaction& tx, const Chain& chain);
    const std::vector<Transaction>& txs() const { return txs_; }
    bool empty() const { return txs_.empty(); }
    size_t size() const { return txs_.size(); }
    void remove_included(const std::vector<Transaction>& included);
private:
    std::vector<Transaction> txs_;
    std::unordered_set<std::string> ids_;
    std::unordered_set<uint64_t> seqs_;
};

std::vector<Transaction> filter_executable(const Balances& committed,
                                           const std::vector<Transaction>& ordered, size_t max_txs);
std::vector<Transaction> select_candidate(const Balances& committed, const Pool& pool,
                                          std::mt19937_64& rng, size_t max_txs);
Block make_candidate_block(const Chain& chain, std::vector<Transaction> txs, bool real_time);
bool accept_block(Chain& chain, const Block& b, std::string& err);

struct CandidateReport {
    int index = 0;
    size_t ntx = 0;
    uint64_t attempts = 0;
    double ms = 0;
    bool solved = false;
    uint64_t nonce = 0;
    std::string hash;
};
struct BlockRace {
    uint64_t height = 0;
    int winner = -1;
    int rounds = 0;
    size_t distinct_txs = 0;
    std::vector<CandidateReport> cands;
};
struct CompResult {
    Chain chain;
    std::vector<BlockRace> races;
    std::vector<RejectedTx> rejected, remaining;
    size_t total_input = 0, included = 0;
    int64_t supply_before = 0, supply_after = 0;
    uint64_t total_attempts = 0;
    double seconds = 0;
    std::string stop_reason;
};
CompResult run_competition(const std::vector<User>& users, const std::vector<Transaction>& input,
                           const Config& cfg, std::ostream* out);

}