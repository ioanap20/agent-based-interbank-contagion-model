/*
 * contagion.cpp
 *
 * Contagion cascade caused by bank defaults.
 *
 * When a bank defaults, its creditors recover only part of the money
 * they lent to it. The remaining unpaid exposure becomes a loss for the
 * creditor banks.
 *
 * These losses reduce the creditors' capital and trigger additional
 * defaults. The process continues iteratively until no new bank defaults.
 *
 * Functions:
 *  - propagating losses from defaulted borrowers to their creditors,
 *  - updating creditor banks' balance sheets,
 *  - identifying newly defaulted banks,
 *  - repeating contagion rounds until the system stabilizes.
 *
 * We use both:
 *  - a sequential version of the contagion algorithm,
 *  - a parallel version used for performance comparison.
 *
 */

// check if we can add more difficult formulas and how does that affect the market and the speedup? Use the commented code

#include "contagion.hpp"
#include "banks.hpp"
#include "output.hpp"

#include <vector>
#include <thread>
#include <algorithm>
#include <utility>
#include <future>
#include <mutex>
#include <condition_variable>
#include <cmath>
#include <string>
#include <iostream>
#include <functional>
#include <memory>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>

/*struct SimpleThreadBarrier{
    std::mutex mtx;
    std::condition_variable cv;
    int total_threads;
    int dynamic_spaces;
    int current_generation;

    SimpleThreadBarrier(int threads) : total_threads(threads), dynamic_spaces(threads), current_generation(0) {}

    void wait() {
        std::unique_lock<std::mutex> lock(mtx);
        int gen=current_generation;
        if(--dynamic_spaces==0){
            current_generation++;
            dynamic_spaces=total_threads;
            cv.notify_all();
        }else{
            cv.wait(lock, [this, gen] {return gen != current_generation; });
        }
    }
};
#include <functional>
#include <condition_variable>
#include <string>
#include <iostream>*/

static const double recovery_rate = 0.40;

static double loan_incoming_payment(double payment_due, double repayment_ratio){
    return payment_due * repayment_ratio;
}

static double loan_credit_loss(double payment_due, double repayment_ratio){
    return payment_due * (1.0 - repayment_ratio);
}

static void add_loan_repayment_effect(const Loan& loan,
                                      double repayment_ratio,
                                      double& incoming_payment,
                                      double* credit_loss){
    incoming_payment += loan_incoming_payment(loan.payment_due, repayment_ratio);
    if(credit_loss){
        *credit_loss += loan_credit_loss(loan.payment_due, repayment_ratio);
    }
}

static double repayment_capacity_ratio(const Bank& bank,
                                       double nominal_liability,
                                       double incoming_payment,
                                       double remaining_asset,
                                       double remaining_liability){
    if(nominal_liability<=0.0){
        return 1.0;
    }

    double liquid_funds = bank.balanceSheet.cash + incoming_payment;
    if(liquid_funds<0.0){
        liquid_funds=0.0;
    }

    double balance_sheet_funds = bank.balanceSheet.cash
                               + incoming_payment
                               + remaining_asset
                               - remaining_liability
                               + bank.balanceSheet.otherAssets
                               - bank.balanceSheet.otherLiabilities;
    if(balance_sheet_funds<0.0){
        balance_sheet_funds=0.0;
    }

    double available_funds = std::min(liquid_funds, balance_sheet_funds);
    double ratio = std::min(1.0, available_funds/nominal_liability);
    if(ratio<0.0){
        ratio=0.0;
    }
    return ratio;
}

static double update_repayment_ratio_for_bank(const Bank& bank,
                                              double nominal_liability,
                                              double incoming_payment,
                                              double remaining_asset,
                                              double remaining_liability,
                                              double previous_ratio,
                                              double& max_difference){
    double current_ratio = repayment_capacity_ratio(
        bank,
        nominal_liability,
        incoming_payment,
        remaining_asset,
        remaining_liability
    );

    double difference = std::abs(current_ratio-previous_ratio);
    if(difference>max_difference){
        max_difference=difference;
    }

    return current_ratio;
}

static void apply_bank_repayment_result(Bank& bank,
                                        double incoming_payment,
                                        double credit_loss,
                                        double nominal_liability,
                                        double repayment_ratio,
                                        double convergence_epsilon){
    if(repayment_ratio<1.0-convergence_epsilon){
        bank.defaulted=true;
    }

    double totalOutgoing=nominal_liability*repayment_ratio;
    bank.balanceSheet.cash+=(incoming_payment-totalOutgoing);

    if(credit_loss>0.0){
        apply_loss(bank, credit_loss);
    }

    if(is_insolvent(bank)){
        bank.defaulted = true;
    }
}

class SpinBarrier{
    std::atomic<int> count{0};
    std::atomic<int> generation{0};
    int participants;

public:
    explicit SpinBarrier(int participantCount) : participants(participantCount){}

    void wait(){
        int seenGeneration = generation.load(std::memory_order_acquire);

        if(count.fetch_add(1, std::memory_order_acq_rel) == participants - 1){
            count.store(0, std::memory_order_release);
            generation.fetch_add(1, std::memory_order_release);
            return;
        }

        while(generation.load(std::memory_order_acquire) == seenGeneration){
            std::this_thread::yield();
        }
    }
};

static int choose_thread_count(int requestedThreads, int amountOfWork){
    if(amountOfWork<=0){
        return 1;
    }
    if(requestedThreads<=0){
        requestedThreads=static_cast<int>(std::thread::hardware_concurrency());
    }
    if(requestedThreads<=0){
        requestedThreads=2;
    }
    return std::min(requestedThreads, amountOfWork);
}

static const int repayment_parallel_threshold = 10000;

static long long elapsed_ns(std::chrono::steady_clock::time_point start,
                            std::chrono::steady_clock::time_point end){
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

static bool parallel_diagnostics_enabled(){
    const char* value = std::getenv("CONTAGION_PARALLEL_DIAGNOSTICS");
    return value && std::string(value) != "0";
}

static std::string parallel_diagnostics_log_path(){
    const char* value = std::getenv("CONTAGION_PARALLEL_LOG");
    if(value && value[0] != '\0'){
        return value;
    }
    return "parallel_diagnostics.log";
}

struct ParallelDiagnostics{
    int num_banks = 0;
    int num_loans = 0;
    int thread_count = 0;
    int active_lenders = 0;
    int iterations = 0;
    long long setup_ns = 0;
    long long solve_ns = 0;
    std::vector<int> lenders_per_thread;
    std::vector<int> loans_per_thread;
    std::vector<long long> incoming_ns;
    std::vector<long long> barrier_ns;
    std::vector<long long> ratio_update_ns;
    std::vector<long long> convergence_ns;
    std::vector<long long> final_accumulate_ns;
    std::vector<long long> final_apply_ns;

    void init(int threads){
        lenders_per_thread.assign(threads, 0);
        loans_per_thread.assign(threads, 0);
        incoming_ns.assign(threads, 0);
        barrier_ns.assign(threads, 0);
        ratio_update_ns.assign(threads, 0);
        convergence_ns.assign(threads, 0);
        final_accumulate_ns.assign(threads, 0);
        final_apply_ns.assign(threads, 0);
    }
};

static long long sum_ns(const std::vector<long long>& values){
    long long total = 0;
    for(long long value : values){
        total += value;
    }
    return total;
}

static long long max_ns(const std::vector<long long>& values){
    long long maximum = 0;
    for(long long value : values){
        maximum = std::max(maximum, value);
    }
    return maximum;
}

static void write_parallel_diagnostics(const ParallelDiagnostics& diagnostics){
    std::ofstream out(parallel_diagnostics_log_path(), std::ios::app);
    if(!out){
        return;
    }

    out << "parallel contagion diagnostics\n";
    out << "banks=" << diagnostics.num_banks
        << " loans=" << diagnostics.num_loans
        << " threads=" << diagnostics.thread_count
        << " active_lenders=" << diagnostics.active_lenders
        << " iterations=" << diagnostics.iterations << '\n';
    out << "setup_ms=" << diagnostics.setup_ns / 1000000.0
        << " solve_ms=" << diagnostics.solve_ns / 1000000.0 << '\n';
    out << "phase_ms_sum"
        << " incoming=" << sum_ns(diagnostics.incoming_ns) / 1000000.0
        << " barrier=" << sum_ns(diagnostics.barrier_ns) / 1000000.0
        << " ratio_update=" << sum_ns(diagnostics.ratio_update_ns) / 1000000.0
        << " convergence=" << sum_ns(diagnostics.convergence_ns) / 1000000.0
        << " final_accumulate=" << sum_ns(diagnostics.final_accumulate_ns) / 1000000.0
        << " final_apply=" << sum_ns(diagnostics.final_apply_ns) / 1000000.0 << '\n';
    out << "phase_ms_max"
        << " incoming=" << max_ns(diagnostics.incoming_ns) / 1000000.0
        << " barrier=" << max_ns(diagnostics.barrier_ns) / 1000000.0
        << " ratio_update=" << max_ns(diagnostics.ratio_update_ns) / 1000000.0
        << " convergence=" << max_ns(diagnostics.convergence_ns) / 1000000.0
        << " final_accumulate=" << max_ns(diagnostics.final_accumulate_ns) / 1000000.0
        << " final_apply=" << max_ns(diagnostics.final_apply_ns) / 1000000.0 << '\n';
    out << "thread lenders loans incoming_ms barrier_ms ratio_update_ms convergence_ms final_accumulate_ms final_apply_ms\n";

    for(int t = 0; t < diagnostics.thread_count; ++t){
        out << t
            << ' ' << diagnostics.lenders_per_thread[t]
            << ' ' << diagnostics.loans_per_thread[t]
            << ' ' << diagnostics.incoming_ns[t] / 1000000.0
            << ' ' << diagnostics.barrier_ns[t] / 1000000.0
            << ' ' << diagnostics.ratio_update_ns[t] / 1000000.0
            << ' ' << diagnostics.convergence_ns[t] / 1000000.0
            << ' ' << diagnostics.final_accumulate_ns[t] / 1000000.0
            << ' ' << diagnostics.final_apply_ns[t] / 1000000.0 << '\n';
    }
    out << '\n';
}

static std::vector<double> compute_nominal_liabilities(const std::vector<Loan>& loans, int numberOfBanks);
static void compute_remaining_obligations(const std::vector<Loan>& loans,
                                          int numberOfBanks,
                                          std::vector<double>& remainingAssets,
                                          std::vector<double>& remainingLiabilities);
static int persistent_team_capacity();

ParallelContagionPlan::ParallelContagionPlan(int numberOfBanks,
                                             const std::vector<Loan>& loans,
                                             int numberOfThreads){
    rebuild(numberOfBanks, loans, numberOfThreads);
}

void ParallelContagionPlan::rebuild(int numberOfBanks,
                                    const std::vector<Loan>& loans,
                                    int numberOfThreads){
    num_banks = numberOfBanks;
    threads = choose_thread_count(numberOfThreads, static_cast<int>(loans.size()));
    if(threads > 1){
        threads = std::min(threads, persistent_team_capacity());
    }

    loans_by_lender.assign(num_banks, {});
    for(int loanIdx = 0; loanIdx < static_cast<int>(loans.size()); ++loanIdx){
        loans_by_lender[loans[loanIdx].lender].push_back(loanIdx);
    }

    std::vector<int> active_lenders;
    active_lenders.reserve(num_banks);
    for(int lender = 0; lender < num_banks; ++lender){
        if(!loans_by_lender[lender].empty()){
            active_lenders.push_back(lender);
        }
    }

    std::sort(active_lenders.begin(), active_lenders.end(), [&](int left, int right){
        return loans_by_lender[left].size() > loans_by_lender[right].size();
    });

    active_lender_count = static_cast<int>(active_lenders.size());
    lenders_by_thread.assign(threads, {});
    loans_per_thread.assign(threads, 0);
    for(int lender : active_lenders){
        int best_thread = 0;
        for(int t = 1; t < threads; ++t){
            if(loans_per_thread[t] < loans_per_thread[best_thread]){
                best_thread = t;
            }
        }

        lenders_by_thread[best_thread].push_back(lender);
        loans_per_thread[best_thread] += static_cast<int>(loans_by_lender[lender].size());
    }

    nominal_liabilities = compute_nominal_liabilities(loans, num_banks);
    compute_remaining_obligations(loans, num_banks, remaining_assets, remaining_liabilities);
    global_incoming.assign(num_banks, 0.0);
    global_credit_losses.assign(num_banks, 0.0);
    local_max_diff.assign(threads, 0.0);
}

bool ParallelContagionPlan::can_run_parallel() const{
    return threads > 1 && num_banks > 0;
}

int ParallelContagionPlan::thread_count() const{
    return threads;
}

class PersistentTeam{
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable job_ready;
    std::atomic<bool> shutdown{false};
    std::atomic<int> job_token{0};
    std::atomic<int> finished{0};
    std::atomic<int> active_threads{0};
    std::function<void(int)>* job_body = nullptr;

    void worker_loop(int thread_id){
        int seen_token = 0;

        while(true){
            {
                std::unique_lock<std::mutex> lock(mutex);
                job_ready.wait(lock, [&](){
                    return shutdown.load(std::memory_order_acquire) || job_token.load(std::memory_order_acquire) != seen_token;
                });

                if(shutdown.load(std::memory_order_acquire)){
                    return;
                }

                seen_token = job_token.load(std::memory_order_acquire);
            }

            if(thread_id < active_threads.load(std::memory_order_acquire) && job_body){
                (*job_body)(thread_id);
                finished.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    }

public:
    PersistentTeam(){
        int pool_size = static_cast<int>(std::thread::hardware_concurrency());
        if(pool_size <= 0){
            pool_size = 4;
        }

        workers.reserve(static_cast<std::size_t>(pool_size - 1));
        for(int thread_id = 1; thread_id < pool_size; ++thread_id){
            workers.emplace_back([this, thread_id](){
                worker_loop(thread_id);
            });
        }
    }

    ~PersistentTeam(){
        {
            std::lock_guard<std::mutex> lock(mutex);
            shutdown.store(true, std::memory_order_release);
            job_token.fetch_add(1, std::memory_order_release);
        }
        job_ready.notify_all();

        for(std::thread& worker : workers){
            if(worker.joinable()){
                worker.join();
            }
        }
    }

    int capacity() const{
        return static_cast<int>(workers.size()) + 1;
    }

    void run(int thread_count, const std::function<void(int)>& body){
        if(thread_count <= 1){
            body(0);
            return;
        }

        thread_count = std::min(thread_count, capacity());
        active_threads.store(thread_count, std::memory_order_release);
        job_body = const_cast<std::function<void(int)>*>(&body);
        finished.store(0, std::memory_order_release);
        job_token.fetch_add(1, std::memory_order_release);
        job_ready.notify_all();

        body(0);

        while(finished.load(std::memory_order_acquire) < thread_count - 1){
            std::this_thread::yield();
        }

        finished.store(0, std::memory_order_release);
    }
};

static PersistentTeam& get_persistent_team(){
    static PersistentTeam team;
    return team;
}

static int persistent_team_capacity(){
    return get_persistent_team().capacity();
}

static void run_parallel_contagion_solver(
    std::vector<Bank>& banks,
    const std::vector<Loan>& loans,
    int threadCount,
    const std::vector<double>& nominal_liabilities,
    const std::vector<double>& remaining_assets,
    const std::vector<double>& remaining_liabilities,
    std::vector<double>& repayment_ratio,
    std::vector<double>& next_ratio,
    ParallelContagionPlan& workspace,
    ParallelDiagnostics* diagnostics){
    const int num_banks = static_cast<int>(banks.size());
    const int max_iterations = 100;
    const double convergence_epsilon = 1e-6;

    SpinBarrier iteration_barrier(threadCount);
    std::atomic<bool> converged{false};

    get_persistent_team().run(threadCount, [&](int thread_id){
        const int bank_begin = static_cast<int>((static_cast<long long>(thread_id) * num_banks) / threadCount);
        const int bank_end = static_cast<int>((static_cast<long long>(thread_id + 1) * num_banks) / threadCount);

        for(int iteration = 0; iteration < max_iterations; ++iteration){
            if(converged.load(std::memory_order_acquire)){
                break;
            }

            std::chrono::steady_clock::time_point phase_start;
            if(diagnostics){
                phase_start = std::chrono::steady_clock::now();
            }
            for(int lender : workspace.lenders_by_thread[thread_id]){
                double incoming = 0.0;
                for(int loan_idx : workspace.loans_by_lender[lender]){
                    const Loan& loan = loans[loan_idx];
                    const double ratio = repayment_ratio[loan.borrower];
                    add_loan_repayment_effect(loan, ratio, incoming, nullptr);
                }
                workspace.global_incoming[lender] = incoming;
            }
            if(diagnostics){
                diagnostics->incoming_ns[thread_id] += elapsed_ns(phase_start, std::chrono::steady_clock::now());
            }

            if(diagnostics){
                phase_start = std::chrono::steady_clock::now();
            }
            iteration_barrier.wait();
            if(diagnostics){
                diagnostics->barrier_ns[thread_id] += elapsed_ns(phase_start, std::chrono::steady_clock::now());
            }

            double thread_max = 0.0;

            if(diagnostics){
                phase_start = std::chrono::steady_clock::now();
            }
            for(int bank_id = bank_begin; bank_id < bank_end; ++bank_id){
                if(nominal_liabilities[bank_id] == 0.0){
                    continue;
                }

                next_ratio[bank_id] = update_repayment_ratio_for_bank(
                    banks[bank_id],
                    nominal_liabilities[bank_id],
                    workspace.global_incoming[bank_id],
                    remaining_assets[bank_id],
                    remaining_liabilities[bank_id],
                    repayment_ratio[bank_id],
                    thread_max
                );
            }
            if(diagnostics){
                diagnostics->ratio_update_ns[thread_id] += elapsed_ns(phase_start, std::chrono::steady_clock::now());
            }

            workspace.local_max_diff[thread_id] = thread_max;
            if(diagnostics){
                phase_start = std::chrono::steady_clock::now();
            }
            iteration_barrier.wait();
            if(diagnostics){
                diagnostics->barrier_ns[thread_id] += elapsed_ns(phase_start, std::chrono::steady_clock::now());
            }

            if(thread_id == 0){
                if(diagnostics){
                    phase_start = std::chrono::steady_clock::now();
                }
                if(diagnostics){
                    diagnostics->iterations++;
                }

                double max_difference = workspace.local_max_diff[0];
                for(int t = 1; t < threadCount; ++t){
                    max_difference = std::max(max_difference, workspace.local_max_diff[t]);
                }

                repayment_ratio.swap(next_ratio);
                if(max_difference < convergence_epsilon){
                    converged.store(true, std::memory_order_release);
                }
                if(diagnostics){
                    diagnostics->convergence_ns[thread_id] += elapsed_ns(phase_start, std::chrono::steady_clock::now());
                }
            }

            if(diagnostics){
                phase_start = std::chrono::steady_clock::now();
            }
            iteration_barrier.wait();
            if(diagnostics){
                diagnostics->barrier_ns[thread_id] += elapsed_ns(phase_start, std::chrono::steady_clock::now());
            }
        }

        std::chrono::steady_clock::time_point phase_start;
        if(diagnostics){
            phase_start = std::chrono::steady_clock::now();
        }
        for(int lender : workspace.lenders_by_thread[thread_id]){
            double incoming = 0.0;
            double credit_losses = 0.0;
            for(int loan_idx : workspace.loans_by_lender[lender]){
                const Loan& loan = loans[loan_idx];
                const double ratio = repayment_ratio[loan.borrower];
                add_loan_repayment_effect(loan, ratio, incoming, &credit_losses);
            }

            workspace.global_incoming[lender] = incoming;
            workspace.global_credit_losses[lender] = credit_losses;
        }
        if(diagnostics){
            diagnostics->final_accumulate_ns[thread_id] += elapsed_ns(phase_start, std::chrono::steady_clock::now());
        }

        if(diagnostics){
            phase_start = std::chrono::steady_clock::now();
        }
        iteration_barrier.wait();
        if(diagnostics){
            diagnostics->barrier_ns[thread_id] += elapsed_ns(phase_start, std::chrono::steady_clock::now());
        }

        if(diagnostics){
            phase_start = std::chrono::steady_clock::now();
        }
        for(int bank_id = bank_begin; bank_id < bank_end; ++bank_id){
            apply_bank_repayment_result(
                banks[bank_id],
                workspace.global_incoming[bank_id],
                workspace.global_credit_losses[bank_id],
                nominal_liabilities[bank_id],
                repayment_ratio[bank_id],
                convergence_epsilon
            );
        }
        if(diagnostics){
            diagnostics->final_apply_ns[thread_id] += elapsed_ns(phase_start, std::chrono::steady_clock::now());
        }
    });
}

static std::vector<double> compute_nominal_liabilities(const std::vector<Loan>& loans, int numberOfBanks){
    std::vector<double> nominalLiabilities(numberOfBanks, 0.0);

    for(const auto& loan:loans){
        nominalLiabilities[loan.borrower]+=loan.payment_due;
    }

    return nominalLiabilities;
}

static void compute_remaining_obligations(const std::vector<Loan>& loans,
                                          int numberOfBanks,
                                          std::vector<double>& remainingAssets,
                                          std::vector<double>& remainingLiabilities){
    remainingAssets.assign(numberOfBanks, 0.0);
    remainingLiabilities.assign(numberOfBanks, 0.0);

    for(const auto& loan:loans){
        remainingAssets[loan.lender]+=loan.remaining;
        remainingLiabilities[loan.borrower]+=loan.remaining;
    }
}

static void apply_repayment_result(std::vector<Bank>& banks, const std::vector<Loan>& loans, const std::vector<double>& nominalLiabilities,
                            const std::vector<double>& repaymentRatio, double convergenceEpsilon){
    int numberOfBanks=static_cast<int>(banks.size());  

    std::vector<double> totalIncoming(numberOfBanks, 0.0);
    std::vector<double> creditLosses(numberOfBanks, 0.0);

    for(const auto& loan:loans){
        double ratio = repaymentRatio[loan.borrower];
        add_loan_repayment_effect(
            loan,
            ratio,
            totalIncoming[loan.lender],
            &creditLosses[loan.lender]
        );
    }

    for(int i=0; i<numberOfBanks; ++i){
        apply_bank_repayment_result(
            banks[i],
            totalIncoming[i],
            creditLosses[i],
            nominalLiabilities[i],
            repaymentRatio[i],
            convergenceEpsilon
        );
    }
}

//find all the banks that are already defaulted at the begining
static std::vector<int> initial_defaulted_banks(const std::vector<Bank>& banks){
    std::vector<int> frontier; //banks that have defaulted but their losses still need to be propagated

    for(int i=0; i<banks.size(); i++){
        if(banks[i].defaulted){
            frontier.push_back(i);
        }
    }

    return frontier;
}

//if bank A defaults, then the banks that lent money to A suffer a loss
//how about the banks that A lent money to? Does anything happen to them?
static void propagate_losses_from_frontier(std::vector<Bank>& banks, const std::vector<Loan>& loans, const LoanIndex& loan_index, const std::vector<int>& frontier){
    /*std::vector<char> is_in_frontier(banks.size(), false);
    for(int bank_id : frontier){
        is_in_frontier[bank_id] = true;
    }*/

    for(int bank_id : frontier){
        for(int loan_idx : loan_index.by_borrower[bank_id]){
            const Loan& loan = loans[loan_idx];
            double loss = (1.0 - recovery_rate) * loan.payment_due;
            // in our parameters for now, the lender recovers 40% so the loss is 60%
            apply_loss(banks[loan.lender], loss);

            if(is_insolvent(banks[loan.lender])){
                banks[loan.lender].defaulted = true;
            }
        }
    }
}

//after one contagion round, some lenders may have become insolvent or illiquid.
static std::vector<int> find_next_frontier(std::vector<Bank>& banks, const LoanIndex& loan_index, const std::vector<char>& already_propagated){
    std::vector<int> next_frontier;

    for(int i = 0; i< banks.size(); i++){
        if(already_propagated[i]) continue;

        if(banks[i].defaulted){
            next_frontier.push_back(i);
            continue;
        }

        double outgoing_payment = loan_index.outgoing_payment[i];
        double incoming_payment = loan_index.incoming_payment[i];

        bool insolvent = is_insolvent(banks[i]);
        bool illiquid = is_illiquid(banks[i], outgoing_payment, incoming_payment);

        if(insolvent || illiquid){
            banks[i].defaulted = true;
            next_frontier.push_back(i);
        }

    }
    return next_frontier;
}

// main contagion sequential algorithm
/*
1. Find banks that defaulted after the shock
2. Propagate their losses
3. Mark them as processed
4. Find newly defaulted banks
5. Repeat until no new defaults
*/
void run_contagion(std::vector<Bank>& banks, const std::vector<Loan>& loans){

    int numberOfBanks = banks.size();

    std::vector<double> nominalLiabilities = compute_nominal_liabilities(loans, numberOfBanks);
    std::vector<double> remainingAssets;
    std::vector<double> remainingLiabilities;
    compute_remaining_obligations(loans, numberOfBanks, remainingAssets, remainingLiabilities);

    std::vector<double> repayment_ratio(numberOfBanks, 1.0);

    const int maxIterations=100;
    const double convergenceEpsilon=1e-6;

    for(int iter=0; iter<maxIterations; ++iter){
        std::vector<double> incomingPayments(numberOfBanks, 0.0);

        for(const auto& loan:loans){
            add_loan_repayment_effect(
                loan,
                repayment_ratio[loan.borrower],
                incomingPayments[loan.lender],
                nullptr
            );
        }

        std::vector<double> next_ratio=repayment_ratio;
        double maxDifference=0.0;

        for(int i=0; i<numberOfBanks; i++){
            if(nominalLiabilities[i]==0.0) continue;
            next_ratio[i]=update_repayment_ratio_for_bank(
                banks[i],
                nominalLiabilities[i],
                incomingPayments[i],
                remainingAssets[i],
                remainingLiabilities[i],
                repayment_ratio[i],
                maxDifference
            );
        }
        repayment_ratio=next_ratio;

        if(maxDifference<convergenceEpsilon) break;
    }


    apply_repayment_result(banks, loans, nominalLiabilities, repayment_ratio, convergenceEpsilon);

    /*std::vector<char> already_propagated(banks.size(), false);
    std::vector<int> frontier = initial_defaulted_banks(banks);

    while(!frontier.empty()){
        propagate_losses_from_frontier(banks, loans, loan_index, frontier);

        for(int bank_id : frontier){
            already_propagated[bank_id] = true;
        }

        frontier = find_next_frontier(banks, loan_index, already_propagated);
    }*/
}

//we make a function that recieves a list of losses and applies them to the banks
static void apply_total_losses(std::vector<Bank>& banks, const std::vector<std::pair<int, double>>& losses){
    std::vector<double> total_losses(banks.size(), 0.0);
    std::vector<char> touched(banks.size(), false);
    std::vector<int> touched_banks;

    for(const auto& entry : losses){
        int bank_id = entry.first;
        double loss = entry.second;

        if(!touched[bank_id]){
            touched[bank_id] = true;
            touched_banks.push_back(bank_id);
        }
        total_losses[bank_id] += loss;
    }

    for(int bank_id : touched_banks){
        apply_loss(banks[bank_id], total_losses[bank_id]);

        if(is_insolvent(banks[bank_id])){
            banks[bank_id].defaulted = true;
        }
    }
}


void run_contagion_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, int numberOfThreads){
    if(numberOfThreads <= 1 || static_cast<int>(loans.size()) < repayment_parallel_threshold){
        run_contagion(banks, loans);
        return;
    }

    const int num_banks = static_cast<int>(banks.size());
    ParallelContagionPlan plan(num_banks, loans, numberOfThreads);
    if(!plan.can_run_parallel()){
        run_contagion(banks, loans);
        return;
    }

    run_contagion_parallel_prepared(banks, loans, plan);
}

void run_contagion_parallel_prepared(
    std::vector<Bank>& banks,
    const std::vector<Loan>& loans,
    ParallelContagionPlan& plan){
    if(static_cast<int>(loans.size()) < repayment_parallel_threshold ||
       !plan.can_run_parallel() ||
       static_cast<int>(banks.size()) != plan.num_banks){
        run_contagion(banks, loans);
        return;
    }

    const int num_banks = plan.num_banks;
    const int threadCount = plan.thread_count();

    ParallelDiagnostics diagnostics;
    ParallelDiagnostics* diagnostics_ptr = nullptr;
    if(parallel_diagnostics_enabled()){
        diagnostics_ptr = &diagnostics;
        diagnostics.init(threadCount);
        diagnostics.num_banks = num_banks;
        diagnostics.num_loans = static_cast<int>(loans.size());
        diagnostics.thread_count = threadCount;
    }

    std::vector<double> repayment_ratio(num_banks, 1.0);
    std::vector<double> next_ratio(num_banks, 1.0);
    if(diagnostics_ptr){
        diagnostics.active_lenders = plan.active_lender_count;
        for(int t = 0; t < threadCount; ++t){
            diagnostics.lenders_per_thread[t] = static_cast<int>(plan.lenders_by_thread[t].size());
            diagnostics.loans_per_thread[t] = plan.loans_per_thread[t];
        }
    }

    std::chrono::steady_clock::time_point solve_start;
    if(diagnostics_ptr){
        solve_start = std::chrono::steady_clock::now();
    }
    run_parallel_contagion_solver(
        banks,
        loans,
        threadCount,
        plan.nominal_liabilities,
        plan.remaining_assets,
        plan.remaining_liabilities,
        repayment_ratio,
        next_ratio,
        plan,
        diagnostics_ptr
    );
    if(diagnostics_ptr){
        diagnostics.solve_ns = elapsed_ns(solve_start, std::chrono::steady_clock::now());
        write_parallel_diagnostics(diagnostics);
    }
}


void run_contagion_small(
    std::vector<Bank>& banks,
    const std::vector<Loan>& loans,
    double cascade_recovery_rate,
    std::vector<char>* persistent_propagated){
    LoanIndex loan_index = build_loan_index(loans, banks.size());

    std::vector<char> local_propagated(banks.size(), false);
    std::vector<char>* already_propagated = persistent_propagated ? persistent_propagated : &local_propagated;

    if(persistent_propagated && persistent_propagated->size() != banks.size()){
        persistent_propagated->assign(banks.size(), false);
    }

    std::vector<int> frontier;

    for(int i = 0; i < static_cast<int>(banks.size()); i++){
        if(banks[i].defaulted && !(*already_propagated)[i]){
            frontier.push_back(i);
        }
    }

    if(persistent_propagated){
        for(int i = 0; i < static_cast<int>(banks.size()); i++){
            if((*already_propagated)[i] || banks[i].defaulted){
                continue;
            }

            double outgoing_payment = loan_index.outgoing_payment[i];
            double incoming_payment = loan_index.incoming_payment[i];

            if(is_insolvent(banks[i]) || is_illiquid(banks[i], outgoing_payment, incoming_payment)){
                banks[i].defaulted = true;
                frontier.push_back(i);
            }
        }
    }

    if(frontier.empty()){
        std::cout<<std::endl;
        std::cout<<"No bank is initially defaulted, so no contagion starts. "<< std::endl;
        return;
    }

    std::cout << std::endl;
    std::cout << "================ CONTAGION CASCADE ================" << std::endl;

    int round = 1;

    while(!frontier.empty()){
        std::cout << std::endl;
        std::cout << "ROUND " << round << std::endl;
        std::cout << std::string(60, '-') << std::endl;
    
        print_frontier_small(frontier);

        std::vector<std::pair<int, double>> lossesThisRound;

        for(int defaultedBorrower : frontier){
            for(int loan_idx : loan_index.by_borrower[defaultedBorrower]){
                const Loan& loan = loans[loan_idx];

                double loss = (1.0 - cascade_recovery_rate) * loan.payment_due;

                lossesThisRound.push_back({loan.lender, loss});

                std::cout << "Borrower bank "
                          << defaultedBorrower
                          << " cannot repay lender bank "
                          << loan.lender
                          << "."
                          << std::endl;
            }
        }

        print_losses_small(lossesThisRound);

        if(!lossesThisRound.empty()){
            apply_total_losses(banks, lossesThisRound);
        }

        for(int bank_id : frontier){
            (*already_propagated)[bank_id] = true;
        }

        frontier = find_next_frontier(banks, loan_index, *already_propagated);

        if(frontier.empty()){
            std::cout << std::endl;
            std::cout << "No new banks defaulted. The system is now stable." << std::endl;
        }

        round++;

    }

        std::cout << "===================================================" << std::endl;

}
