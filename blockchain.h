#pragma once
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bc {

constexpr uint32_t kVersion = 1;
constexpr int64_t kGenesisTimestamp = 1761696000;
constexpr int64_t kBlockInterval = 600;

using Balances = std::unordered_map<std::string, int64_t>;

struct User {
    std::string name;
    std::string public_key;
    int64_t balance = 0;
};

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
std::string compute_tx_root(const std::vector<Transaction>& txs);
std::string serialize_header(const BlockHeader& h);
std::string compute_header_hash(const BlockHeader& h);
bool meets_difficulty(const std::string& hash, uint32_t difficulty);
Block make_genesis();

std::string check_tx_structure(const Transaction& tx, const Balances& accounts);
ApplyStatus apply_tx(const Transaction& tx, Balances& state);
int64_t total_supply(const Balances& state);
bool verify_block(const Block& b, const Block& prev, const Balances& state,
                  const std::unordered_set<std::string>& seen_ids, uint32_t expected_difficulty,
                  std::string& err);
bool validate_chain(const std::vector<Block>& chain, const Balances& initial,
                    uint32_t expected_difficulty, std::string& err);

uint64_t mine_block(Block& b);

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
};

struct BlockStats {
    uint64_t attempts = 0;
    double ms = 0;
};

struct RejectedTx {
    Transaction tx;
    std::string reason;
};

struct SimResult {
    std::vector<Block> chain;
    std::vector<BlockStats> stats;
    Balances initial, final_state;
    std::vector<RejectedTx> rejected;
    std::vector<RejectedTx> remaining;
    size_t total_input = 0, included = 0;
    int64_t supply_before = 0, supply_after = 0;
    uint64_t total_attempts = 0;
    double seconds = 0;
};

SimResult run_simulation(const std::vector<User>& users, const std::vector<Transaction>& input,
                         const Config& cfg, std::ostream* out);

}  // namespace bc