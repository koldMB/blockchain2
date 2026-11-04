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

// formato versijos: 1 = v0.1 (tx_root = myhash(id konkatenacija)); 2 = v0.2 (merkle saknis)
constexpr uint32_t kVersionLegacy = 1;
constexpr uint32_t kVersionMerkle = 2;
constexpr uint32_t kVersion = kVersionMerkle;  // dabartine (numatytoji) versija
bool is_supported_version(uint32_t v);
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
bool is_lower_hex(const std::string& s);    // netuscia, tik 0-9a-f
bool is_valid_tx_id(const std::string& s);  // 64 mazosios hex raides
std::string compute_tx_root(const std::vector<Transaction>& txs, uint32_t version = kVersion);
std::string serialize_header(const BlockHeader& h);
std::string compute_header_hash(const BlockHeader& h);
bool meets_difficulty(const std::string& hash, uint32_t difficulty);
Block make_genesis(uint32_t version = kVersion);

std::string check_tx_structure(const Transaction& tx, const Balances& accounts);
ApplyStatus apply_tx(const Transaction& tx, Balances& state);
int64_t total_supply(const Balances& state);
bool verify_block(const Block& b, const Block& prev, const Balances& state,
                  const std::unordered_set<std::string>& seen_ids, uint32_t expected_difficulty,
                  std::string& err, size_t max_txs = SIZE_MAX);
bool validate_chain(const std::vector<Block>& chain, const Balances& initial,
                    uint32_t expected_difficulty, std::string& err, size_t max_txs = SIZE_MAX);

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
    // --- v0.2 ---
    uint32_t version = kVersion;      // 2 = merkle, 1 = v0.1 tx_root
    size_t num_candidates = 5;        // kandidatu skaicius tam paciam aukstui
    uint64_t initial_budget = 2000;   // bandymu biudzetas kandidatui 1-ame raunde
    uint64_t budget_growth = 2;       // biudzeto didinimo koeficientas kiekvienam kitam raundui
    uint64_t max_attempts = 2000000;  // bendra bandymu riba kandidatui (aiskus sustabdymas)
    double time_limit_s = 5.0;        // bendra kasimo laiko riba kandidatui, sekundemis
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

// v0.1 nuoseklus kasejas (paliktas suderinamumui; --mode legacy)
SimResult run_simulation(const std::vector<User>& users, const std::vector<Transaction>& input,
                         const Config& cfg, std::ostream* out);

// v0.2 grandine, pool, kandidatai

struct ChainParams {
    uint32_t version = kVersion;
    uint32_t difficulty = 3;  // nustato GRANDINE, ne kandidatas
    size_t max_txs = 100;     // leidziamas transakciju skaicius bloke
};

struct Chain {
    ChainParams params;
    std::vector<Block> blocks;  // blocks[0] = genesis
    Balances initial;           // pradine busena (atkurimui / pilnai patikrai)
    Balances balances;          // patvirtinta busena
    std::unordered_set<std::string> ids;  // itrauktu transakciju id
    std::unordered_set<uint64_t> seqs;    // itrauktu transakciju sequence
    const Block& tip() const { return blocks.back(); }
};
Chain make_chain(const std::vector<User>& users, const ChainParams& params);

// transakciju fondas. priemimas i pool NEREZERVUOJA siuntejo lesu.
class Pool {
public:
    // 1) pirmine patikra: formatas, dalyviai, suma, id, dublikatai fonde ir grandineje.
    //    grazina "" (priimta) arba atmetimo priezasti.
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

// 2) kandidato atranka: nuosekliai taiko duota tvarka laikinoje busenos kopijoje;
//    neivykdomos (pvz. bendras poreikis virsija balansa) praleidziamos. iki max_txs.
std::vector<Transaction> filter_executable(const Balances& committed,
                                           const std::vector<Transaction>& ordered, size_t max_txs);
// atsitiktine (be pasikartojimu) pool permutacija + ta pati logika.
std::vector<Transaction> select_candidate(const Balances& committed, const Pool& pool,
                                          std::mt19937_64& rng, size_t max_txs);
Block make_candidate_block(const Chain& chain, std::vector<Transaction> txs, bool real_time);
// bando nonce [next_nonce, next_nonce + budget). testiama nuo dar netikrintu nonce.
bool mine_range(Block& b, uint64_t& next_nonce, uint64_t budget, uint64_t& used);
// 3) galutinis priemimas: pilna patikra ant kopijos; tik sekmes atveju vienu veiksmu atnaujina
//    grandine, balansus ir itrauktu aibes. nesekmes atveju nieko nekeicia.
bool accept_block(Chain& chain, const Block& b, std::string& err);

struct CandidateReport {
    int index = 0;  // 1..N
    size_t ntx = 0;
    uint64_t attempts = 0;
    double ms = 0;
    bool solved = false;
    uint64_t nonce = 0;
    std::string hash;
};
struct BlockRace {
    uint64_t height = 0;
    int winner = -1;  // indeksas cands vektoriuje, -1 = nera
    int rounds = 0;   // kiek biudzeto raundu prireike
    size_t distinct_txs = 0;  // skirtingu transakciju visuose kandidatuose (persidengimas)
    std::vector<CandidateReport> cands;
};
struct CompResult {
    Chain chain;
    std::vector<BlockRace> races;
    std::vector<RejectedTx> rejected;   // atmesta pries patenkant i pool
    std::vector<RejectedTx> remaining;  // liko pool pabaigoje su priezastimi
    size_t total_input = 0, included = 0;
    int64_t supply_before = 0, supply_after = 0;
    uint64_t total_attempts = 0;  // visu kandidatu
    double seconds = 0;
    std::string stop_reason;  // tuscia = pool istustejo
};
CompResult run_competition(const std::vector<User>& users, const std::vector<Transaction>& input,
                           const Config& cfg, std::ostream* out);

}  // namespace bc
