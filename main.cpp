#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>

#include "blockchain.h"

using namespace bc;

static void usage() {
    std::cout << "Naudojimas: ./blockchain [--mode compete|legacy] [--version 1|2]\n"
                 "   [--users N] [--txs N] [--seed S] [--difficulty D] [--block-size B]\n"
                 "   [--candidates N] [--budget N] [--growth N] [--max-attempts N] [--time-limit SEK]\n"
                 "   [--max-defers M] [--max-blocks N] [--no-invalid] [--real-time] [--full-hash] [--quiet]\n"
                 "   [--bench-mine N]  (v0.3: palygina sena ir optimizuota kasima, N bandymu)\n";
}

static void write_rejected(const std::vector<RejectedTx>& a, const std::vector<RejectedTx>& b) {
    std::ofstream f("rejected.csv");
    f << "sequence,id,sender,receiver,amount,reason\n";
    for (const auto* v : {&a, &b})
        for (const auto& x : *v)
            f << x.tx.sequence << ',' << x.tx.id << ',' << x.tx.sender << ',' << x.tx.receiver << ','
              << x.tx.amount << ",\"" << x.reason << "\"\n";
}

static void print_reasons(const std::vector<RejectedTx>& v, const std::string& title) {
    std::map<std::string, size_t> by;
    for (const auto& x : v) {
        std::string k = x.reason.rfind("nepakanka lesu", 0) == 0 ? "nepakanka lesu" : x.reason;
        ++by[k];
    }
    std::cout << title << "\n";
    for (const auto& kv : by) std::cout << "  - " << kv.first << ": " << kv.second << "\n";
}

int main(int argc, char** argv) {
    Config cfg;
    std::string mode = "compete";
    try {
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error("truksta reiksmes: " + a);
                return argv[++i];
            };
            if (a == "--mode") mode = next();
            else if (a == "--version") cfg.version = static_cast<uint32_t>(std::stoul(next()));
            else if (a == "--users") cfg.num_users = std::stoull(next());
            else if (a == "--txs") cfg.num_txs = std::stoull(next());
            else if (a == "--seed") cfg.seed = std::stoull(next());
            else if (a == "--difficulty") cfg.difficulty = static_cast<uint32_t>(std::stoul(next()));
            else if (a == "--block-size") cfg.block_size = std::stoull(next());
            else if (a == "--candidates") cfg.num_candidates = std::stoull(next());
            else if (a == "--budget") cfg.initial_budget = std::stoull(next());
            else if (a == "--growth") cfg.budget_growth = std::stoull(next());
            else if (a == "--max-attempts") cfg.max_attempts = std::stoull(next());
            else if (a == "--time-limit") cfg.time_limit_s = std::stod(next());
            else if (a == "--max-defers") cfg.max_defers = std::stoi(next());
            else if (a == "--max-blocks") cfg.max_blocks = std::stoull(next());
            else if (a == "--no-invalid") cfg.inject_invalid = false;
            else if (a == "--real-time") cfg.real_time = true;
            else if (a == "--full-hash") cfg.full_hash = true;
            else if (a == "--quiet") cfg.verbose = false;
            else if (a == "--bench-mine") cfg.bench_mine = std::stoull(next());
            else if (a == "--help" || a == "-h") { usage(); return 0; }
            else throw std::runtime_error("nezinomas argumentas: " + a);
        }

        // v0.3: benchmark mode — runs before the simulation, exits early.
        if (cfg.bench_mine > 0) {
            BlockHeader h;
            h.prev_hash = std::string(64, 'a');
            h.timestamp = kGenesisTimestamp;
            h.version = 2;
            h.tx_root = std::string(64, 'b');
            h.nonce = 0;
            h.difficulty = 8;  // so no nonce in [0, N) succeeds
            BenchResult r = bench_mining(h, cfg.bench_mine);
            std::cout << "=== Kasimo benchmark (v0.3) ===\n"
                      << "N = " << r.attempts << " bandymu, fiksuota antrašte\n"
                      << "v0.2 kelias:  " << std::fixed << std::setprecision(3) << r.old_ms << " ms ("
                      << std::setprecision(0) << (r.old_ms > 0 ? r.attempts * 1000.0 / r.old_ms : 0) << " h/s)\n"
                      << "v0.3 kelias:  " << std::fixed << std::setprecision(3) << r.new_ms << " ms ("
                      << std::setprecision(0) << (r.new_ms > 0 ? r.attempts * 1000.0 / r.new_ms : 0) << " h/s)\n"
                      << "pagreitis:    " << std::setprecision(2)
                      << (r.new_ms > 0 ? r.old_ms / r.new_ms : 0) << "x\n";
            return 0;
        }

        if ((mode != "compete" && mode != "legacy") || !is_supported_version(cfg.version) || cfg.block_size == 0 ||
            cfg.num_users < 2 || cfg.max_defers < 1 || cfg.difficulty > 8 || cfg.num_candidates == 0 ||
            cfg.initial_budget == 0 || cfg.budget_growth < 1 || cfg.max_attempts == 0)
            throw std::runtime_error("netinkami parametrai");

        std::cout << "=== Supaprastinta bloku grandine v0.3 (rezimas: " << mode << ") ===\n"
                  << "seed=" << cfg.seed << " users=" << cfg.num_users << " txs=" << cfg.num_txs
                  << " block_size=" << cfg.block_size << " difficulty=" << cfg.difficulty
                  << " hash=myhash256 v2.0 formato versija=" << cfg.version
                  << (cfg.version == kVersionMerkle ? " (Merkle saknis)" : " (v0.1 tx_root)") << "\n";
        if (mode == "compete")
            std::cout << "kandidatu=" << cfg.num_candidates << " pradinis biudzetas=" << cfg.initial_budget
                      << " bandymu/raunde, x" << cfg.budget_growth << " kas raunda; riba kandidatui: "
                      << cfg.max_attempts << " bandymu arba " << cfg.time_limit_s << " s\n";

        std::vector<User> users = generate_users(cfg.num_users, cfg.seed);
        std::vector<Transaction> txs = generate_transactions(users, cfg.num_txs, cfg.seed, cfg.inject_invalid);
        {
            std::ofstream f("initial_state.csv");
            f << "name,public_key,balance\n";
            for (const auto& u : users) f << u.name << ',' << u.public_key << ',' << u.balance << '\n';
        }
        std::cout << "Pradine busena issaugota: initial_state.csv\n";
        const Block g = make_genesis(cfg.version);
        std::cout << "Genesis: hash=" << g.hash << "\n         prev_hash=" << g.header.prev_hash
                  << "\n         tx_root=" << g.header.tx_root << " (tuscio atvejis = H(tuscia)), difficulty=0 (PoW isimtis)\n\n";

        if (mode == "legacy") {
            SimResult r = run_simulation(users, txs, cfg, &std::cout);
            write_rejected(r.rejected, r.remaining);
            std::string err;
            bool ok = validate_chain(r.chain, r.initial, cfg.difficulty, err, cfg.block_size);
            std::cout << "\n=== Galutine suvestine ===\n"
                      << "Vartotoju: " << users.size() << "\nSugeneruota transakciju: " << r.total_input << "\n"
                      << "Priimta bloku (be genesis): " << r.chain.size() - 1 << "\nItraukta: " << r.included
                      << "\nAtmesta: " << r.rejected.size() << " (rejected.csv)\nLikusiu fonde: " << r.remaining.size()
                      << "\nPradine lesu suma: " << r.supply_before << "\nGalutine lesu suma: " << r.supply_after
                      << (r.supply_before == r.supply_after ? "  (sutampa)" : "  (KLAIDA!)")
                      << "\nKasimo bandymu: " << r.total_attempts << "\nBendras laikas: " << std::fixed
                      << std::setprecision(3) << r.seconds << " s\nPilnos grandines patikra: "
                      << (ok ? "OK" : "KLAIDA: " + err) << "\n";
            print_reasons(r.rejected, "Atmetimo priezastys:");
            return ok ? 0 : 2;
        }

        CompResult r = run_competition(users, txs, cfg, &std::cout);
        write_rejected(r.rejected, r.remaining);
        {
            std::ofstream f("candidates.csv");
            f << "height,candidate,txs,attempts,ms,solved,winner\n";
            for (const auto& race : r.races)
                for (size_t i = 0; i < race.cands.size(); ++i) {
                    const auto& c = race.cands[i];
                    f << race.height << ",K" << c.index << ',' << c.ntx << ',' << c.attempts << ',' << std::fixed
                      << std::setprecision(3) << c.ms << ',' << (c.solved ? 1 : 0) << ','
                      << (static_cast<int>(i) == race.winner ? 1 : 0) << '\n';
                }
        }

        std::string err;
        bool ok = validate_chain(r.chain.blocks, r.chain.initial, cfg.difficulty, err, cfg.block_size);

        std::vector<size_t> wins(cfg.num_candidates + 1, 0);
        std::vector<uint64_t> att(cfg.num_candidates + 1, 0);
        std::vector<double> ms(cfg.num_candidates + 1, 0);
        uint64_t win_attempts = 0;
        size_t multi_round = 0, max_round = 0, distinct_sum = 0, cand_sum = 0;
        for (const auto& race : r.races) {
            if (race.rounds > 1) ++multi_round;
            max_round = std::max<size_t>(max_round, static_cast<size_t>(race.rounds));
            distinct_sum += race.distinct_txs;
            for (const auto& c : race.cands) {
                cand_sum += c.ntx;
                att[static_cast<size_t>(c.index)] += c.attempts;
                ms[static_cast<size_t>(c.index)] += c.ms;
            }
            if (race.winner >= 0) {
                ++wins[static_cast<size_t>(race.cands[static_cast<size_t>(race.winner)].index)];
                win_attempts += race.cands[static_cast<size_t>(race.winner)].attempts;
            }
        }

        std::cout << "\n=== Kandidatu bandymu / laiko suvestine (visi blokai) ===\n"
                  << "Kandidatas | laimejimu | bandymu | kasimo laikas, s\n";
        for (size_t k = 1; k <= cfg.num_candidates; ++k)
            std::cout << "  K" << k << "       | " << std::setw(9) << wins[k] << " | " << std::setw(7) << att[k] << " | "
                      << std::fixed << std::setprecision(3) << ms[k] / 1000.0 << "\n";
        std::cout << "Bloku, kuriems prireike didesnio biudzeto (>1 raundo): " << multi_round << " is " << r.races.size()
                  << " (daugiausia raundu: " << max_round << ")\n"
                  << "Laimetoju bandymai: " << win_attempts << ", visu kandidatu bandymai: " << r.total_attempts
                  << " (nelaimejusiu kandidatu darbas atmestas: "
                  << (r.total_attempts ? 100.0 * static_cast<double>(r.total_attempts - win_attempts) / static_cast<double>(r.total_attempts) : 0.0)
                  << " %)\n"
                  << "Kandidatu persidengimas: " << distinct_sum << " skirtingu transakciju is " << cand_sum
                  << " kandidatu vietu (kandidatai gali persidengti)\n"
                  << "Detali lentele: candidates.csv\n";

        std::cout << "\n=== Galutine suvestine ===\n"
                  << "Vartotoju: " << users.size() << "\n"
                  << "Sugeneruota transakciju (iskaitant demonstracines netinkamas): " << r.total_input << "\n"
                  << "Priimta bloku (be genesis): " << r.chain.blocks.size() - 1 << "\n"
                  << "Itraukta transakciju: " << r.included << "\n"
                  << "Atmesta pries pool (netinkamos): " << r.rejected.size() << "\n"
                  << "Likusiu fonde (neivykdomos): " << r.remaining.size() << " (priezastys: rejected.csv)\n"
                  << "Sustabdymo priezastis: " << (r.stop_reason.empty() ? "pool istustejo" : r.stop_reason) << "\n"
                  << "Pradine lesu suma: " << r.supply_before << "\n"
                  << "Galutine lesu suma: " << r.supply_after << (r.supply_before == r.supply_after ? "  (sutampa)" : "  (KLAIDA!)") << "\n"
                  << "Bendras laikas: " << std::fixed << std::setprecision(3) << r.seconds << " s\n"
                  << "Pilnos grandines patikra nuo pradines busenos: " << (ok ? "OK" : "KLAIDA: " + err) << "\n";
        print_reasons(r.rejected, "Atmetimo priezastys (pries pool):");
        print_reasons(r.remaining, "Likusiu priezastys:");
        return ok ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "Klaida: " << e.what() << "\n";
        usage();
        return 1;
    }
}