#pragma once

#ifndef CONTAGION_HPP
#define CONTAGION_HPP

#include <vector>
#include "banks.hpp"
#include "market.hpp"

void run_contagion(std::vector<Bank>& banks, const std::vector<Loan>& loans);

class ParallelContagionPlan{
public:
    ParallelContagionPlan() = default;
    ParallelContagionPlan(int numberOfBanks, const std::vector<Loan>& loans, int numberOfThreads);

    void rebuild(int numberOfBanks, const std::vector<Loan>& loans, int numberOfThreads);
    bool can_run_parallel() const;
    int thread_count() const;

    // Prepared data is exposed for the contagion implementation; callers should treat it as read-only.
    int num_banks = 0;
    int threads = 1;
    int active_lender_count = 0;
    std::vector<std::vector<int>> loans_by_lender;
    std::vector<std::vector<int>> lenders_by_thread;
    std::vector<int> loans_per_thread;
    std::vector<double> nominal_liabilities;
    std::vector<double> remaining_assets;
    std::vector<double> remaining_liabilities;
    std::vector<double> global_incoming;
    std::vector<double> global_credit_losses;
    std::vector<double> local_max_diff;

    friend void run_contagion_parallel_prepared(
        std::vector<Bank>& banks,
        const std::vector<Loan>& loans,
        ParallelContagionPlan& plan
    );
};

void run_contagion_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, int numberOfThreads);
void run_contagion_parallel_prepared(
    std::vector<Bank>& banks,
    const std::vector<Loan>& loans,
    ParallelContagionPlan& plan
);

double total_outgoing_payment(int bank_id, const std::vector<Loan>& loans);
double total_incoming_payment(int bank_id, const std::vector<Loan>& loans);

void run_contagion_small(
    std::vector<Bank>& banks,
    const std::vector<Loan>& loans,
    double recovery_rate = 0.30,
    std::vector<char>* persistent_propagated = nullptr
);

#endif
