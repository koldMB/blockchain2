#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "blockchain.h"

using namespace bc;

static void usage() {
    std::cout << "naudojimas: ./blockchain [--users n] [--txs n] [--seed s] [--difficulty d]\n"
                 "                         [--block-size b] [--max-defers m] [--max-blocks n]\n"
                 "                         [--no-invalid] [--real-time] [--full-hash] [--quiet]\n";
}

int main(int argc, char** argv) {
    Config cfg;
    try {
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error("truksta reiksmes: " + a);
                return argv[++i];
            };
            if (a == "--users") cfg.num_users = std::stoull(next());
            else if (a == "--txs") cfg.num_txs = std::stoull(next());
            else if (a == "--seed") cfg.seed = std::stoull(next());
            else if (a == "--difficulty") cfg.difficulty = static_cast<uint32_t>(std::stoul(next()));
            else if (a == "--block-size") cfg.block_size = std::stoull(next());
            else if (a == "--max-defers") cfg.max_defers = std::stoi(next());
            else if (a == "--max-blocks") cfg.max_blocks = std::stoull(next());
            else if (a == "--no-invalid") cfg.inject_invalid = false;
            else if (a == "--real-time") cfg.real_time = true;
            else if (a == "--full-hash") cfg.full_hash = true;
            else if (a == "--quiet") cfg.verbose = false;
            else if (a == "--help" || a == "-h") { usage(); return 0; }
            else throw std::runtime_error("nezinomas argumentas: " + a);
        }
        if (cfg.block_size == 0 || cfg.num_users < 2 || cfg.max_defers < 1 || cfg.difficulty > 8)
            throw std::runtime_error("netinkami parametrai");

        std::cout << "=== supaprastinta bloku grandine v0.1 ===\n"
                  << "seed=" << cfg.seed << " users=" << cfg.num_users << " txs=" << cfg.num_txs
                  << " block_size=" << cfg.block_size << " difficulty=" << cfg.difficulty
                  << " max_defers=" << cfg.max_defers << " hash=myhash256 v2.0 versija=" << kVersion << "\n";

        std::vector<User> users = generate_users(cfg.num_users, cfg.seed);
        std::vector<Transaction> txs = generate_transactions(users, cfg.num_txs, cfg.seed, cfg.inject_invalid);

        {
            std::ofstream f("initial_state.csv");
            f << "name,public_key,balance\n";
            for (const auto& u : users) f << u.name << ',' << u.public_key << ',' << u.balance << '\n';
        }
        std::cout << "pradine busena issaugota: initial_state.csv\n";
        const Block g = make_genesis();
        std::cout << "genesis: hash=" << g.hash << "\n         prev_hash=" << g.header.prev_hash
                  << "\n         tx_root=" << g.header.tx_root << " (h(tuscia)), difficulty=0 (pow isimtis)\n\n";

        SimResult r = run_simulation(users, txs, cfg, &std::cout);

        {
            std::ofstream f("rejected.csv");
            f << "sequence,id,sender,receiver,amount,reason\n";
            for (const auto& x : r.rejected)
                f << x.tx.sequence << ',' << x.tx.id << ',' << x.tx.sender << ',' << x.tx.receiver << ','
                  << x.tx.amount << ",\"" << x.reason << "\"\n";
            for (const auto& x : r.remaining)
                f << x.tx.sequence << ',' << x.tx.id << ',' << x.tx.sender << ',' << x.tx.receiver << ','
                  << x.tx.amount << ",\"" << x.reason << "\"\n";
        }

        std::string err;
        bool ok = validate_chain(r.chain, r.initial, cfg.difficulty, err);

        std::cout << "\n=== galutine suvestine ===\n"
                  << "vartotoju: " << users.size() << "\n"
                  << "sugeneruota transakciju (iskaitant demonstracines netinkamas): " << r.total_input << "\n"
                  << "priimta bloku (be genesis): " << r.chain.size() - 1 << "\n"
                  << "itraukta transakciju: " << r.included << "\n"
                  << "atmesta transakciju: " << r.rejected.size() << " (priezastys: rejected.csv)\n"
                  << "likusiu fonde: " << r.remaining.size() << "\n"
                  << "pradine lesu suma: " << r.supply_before << "\n"
                  << "galutine lesu suma: " << r.supply_after << (r.supply_before == r.supply_after ? "  (sutampa)" : "  (klaida!)") << "\n"
                  << "kasimo bandymu is viso: " << r.total_attempts << "\n"
                  << "bendras laikas: " << std::fixed << std::setprecision(3) << r.seconds << " s\n"
                  << "piltnos grandines patikra nuo pradines busenos: " << (ok ? "ok" : "klaida: " + err) << "\n";

        std::unordered_map<std::string, size_t> by_reason;
        for (const auto& x : r.rejected) {
            std::string key = x.reason.rfind("nepakanka lesu", 0) == 0 ? "nepakanka lesu (po atidejimu)" : x.reason;
            ++by_reason[key];
        }
        for (const auto& kv : by_reason) std::cout << "  - " << kv.first << ": " << kv.second << "\n";
        return ok ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "klaida: " << e.what() << "\n";
        usage();
        return 1;
    }
}