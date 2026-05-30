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

#include "contagion.hpp"

#include <vector>
#include <thread>
#include <algorithm>
#include <utility>

static const double recovery_rate = 0.4;

struct LoanIndex {
    std::vector<std::vector<int>> by_borrower;
    std::vector<double> outgoing_payment;
    std::vector<double> incoming_payment;
};

static LoanIndex build_loan_index(const std::vector<Loan>& loans, int num_banks){
    LoanIndex index;
    index.by_borrower.assign(num_banks, {});
    index.outgoing_payment.assign(num_banks, 0.0);
    index.incoming_payment.assign(num_banks, 0.0);

    for(int loan_idx = 0; loan_idx < static_cast<int>(loans.size()); loan_idx++){
        const Loan& loan = loans[loan_idx];
        index.by_borrower[loan.borrower].push_back(loan_idx);
        index.outgoing_payment[loan.borrower] += loan.payment_due;
        index.incoming_payment[loan.lender] += loan.payment_due;
    }

    return index;
}

double total_outgoing_payment(int bank_id, const std::vector<Loan>& loans){
    double total = 0.0;

    for(const Loan& loan : loans){
        if(loan.borrower == bank_id){
            total += loan.payment_due;
        }
    }

    return total;
}

double total_incoming_payment(int bank_id, const std::vector<Loan>& loans){
    double total = 0.0;

    for(const Loan& loan : loans){
        if(loan.lender == bank_id){
            total += loan.payment_due;
        }
    }

    return total;
}

static int choose_thread_count(int requestedThreads, int amountOfWork){
    if(amountOfWork <= 0){
        return 1;
    }

    if(requestedThreads <= 0){
        requestedThreads = static_cast<int>(std::thread::hardware_concurrency());
    }

    if(requestedThreads <= 0){
        requestedThreads = 2;
    }

    return std::min(requestedThreads, amountOfWork);
}

static std::vector<int> initial_defaulted_banks(const std::vector<Bank>& banks){
    std::vector<int> frontier;

    for(int i = 0; i < static_cast<int>(banks.size()); i++){
        if(banks[i].defaulted){
            frontier.push_back(i);
        }
    }

    return frontier;
}

static void propagate_losses_from_frontier(
    std::vector<Bank>& banks,
    const std::vector<Loan>& loans,
    const LoanIndex& loan_index,
    const std::vector<int>& frontier
){
    std::vector<char> is_in_frontier(banks.size(), false);

    for(int bank_id : frontier){
        is_in_frontier[bank_id] = true;
    }

    for(int bank_id : frontier){
        for(int loan_idx : loan_index.by_borrower[bank_id]){
            const Loan& loan = loans[loan_idx];
            double loss = (1.0 - recovery_rate) * loan.payment_due;

            apply_loss(banks[loan.lender], loss);

            if(is_insolvent(banks[loan.lender])){
                banks[loan.lender].defaulted = true;
            }
        }
    }
}

static std::vector<int> find_next_frontier(
    std::vector<Bank>& banks,
    const LoanIndex& loan_index,
    const std::vector<char>& already_propagated
){
    std::vector<int> next_frontier;

    for(int i = 0; i < static_cast<int>(banks.size()); i++){
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

void run_contagion(std::vector<Bank>& banks, const std::vector<Loan>& loans){
    LoanIndex loan_index = build_loan_index(loans, static_cast<int>(banks.size()));
    std::vector<char> already_propagated(banks.size(), false);
    std::vector<int> frontier = initial_defaulted_banks(banks);

    while(!frontier.empty()){
        propagate_losses_from_frontier(banks, loans, loan_index, frontier);

        for(int bank_id : frontier){
            already_propagated[bank_id] = true;
        }

        frontier = find_next_frontier(banks, loan_index, already_propagated);
    }
}

static void apply_total_losses(std::vector<Bank>& banks, const std::vector<std::pair<int, double>>& losses){
    std::vector<double> total_losses(banks.size(), 0.0);
    std::vector<char> touched(banks.size(), false);
    std::vector<int>touched_banks;

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

static void propagate_losses_from_frontier_parallel(
    std::vector<Bank>& banks,
    const std::vector<Loan>& loans,
    const LoanIndex& loan_index,
    const std::vector<int>& frontier,
    int numberOfThreads
){
    std::vector<int> active_loans;

    for(int bank_id : frontier){
        for(int loan_idx : loan_index.by_borrower[bank_id]){
            active_loans.push_back(loan_idx);
        }
    }

    if(active_loans.empty()){
        return;
    }

    const int parallel_threshold = 2000;

    if(static_cast<int>(active_loans.size()) < parallel_threshold || numberOfThreads <= 1){
        std::vector<std::pair<int, double>>losses;
        losses.reserve(active_loans.size());

        for(int loan_idx : active_loans){
            const Loan& loan = loans[loan_idx];
            double loss = (1.0 - recovery_rate) * loan.payment_due;
            losses.push_back({loan.lender, loss});
        }
        apply_total_losses(banks, losses);
        return;
    }


    int threadCount = choose_thread_count(numberOfThreads, active_loans.size());
    

    std::vector<std::vector<std::pair<int, double>>> local_losses(threadCount);

    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for(int thread_id = 0; thread_id < threadCount; thread_id++){
        int begin = static_cast<int>(active_loans.size()) * thread_id / threadCount;
        int end = static_cast<int>(active_loans.size()) * (thread_id + 1) / threadCount;
        
        local_losses[thread_id].reserve(end - begin);

        threads.emplace_back([&, thread_id, begin, end](){
            for(int i = begin; i < end; i++){
                int loan_idx = active_loans[i];
                const Loan& loan = loans[loan_idx];

                double loss = (1.0 - recovery_rate) * loan.payment_due;

                local_losses[thread_id].push_back({loan.lender, loss});

            }
        });
    }

    for(std::thread& worker : threads){
        worker.join();
    }

    std::vector<std::pair<int, double>> all_losses;
    all_losses.reserve(active_loans.size());

    for(int thread_id = 0; thread_id < threadCount; thread_id++){
        for(const auto& entry : local_losses[thread_id]){
            all_losses.push_back(entry);
        }
    }

    apply_total_losses(banks, all_losses);

}


static std::vector<int> find_next_frontier_parallel(
    std::vector<Bank>& banks,
    const LoanIndex& loan_index,
    const std::vector<char>& already_propagated,
    int numberOfThreads
){
    int threadCount = choose_thread_count(numberOfThreads, static_cast<int>(banks.size()));
    std::vector<std::vector<int>> local_frontiers(threadCount);
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for(int thread_id = 0; thread_id < threadCount; thread_id++){
        int begin = static_cast<int>(banks.size()) * thread_id / threadCount;
        int end = static_cast<int>(banks.size()) * (thread_id + 1) / threadCount;

        threads.emplace_back([&, thread_id, begin, end](){
            for(int bank_id = begin; bank_id < end; bank_id++){
                if(already_propagated[bank_id]) continue;

                if(banks[bank_id].defaulted){
                    local_frontiers[thread_id].push_back(bank_id);
                    continue;
                }

                double outgoing_payment = loan_index.outgoing_payment[bank_id];
                double incoming_payment = loan_index.incoming_payment[bank_id];

                bool insolvent = is_insolvent(banks[bank_id]);
                bool illiquid = is_illiquid(banks[bank_id], outgoing_payment, incoming_payment);

                if(insolvent || illiquid){
                    banks[bank_id].defaulted = true;
                    local_frontiers[thread_id].push_back(bank_id);
                }
            }
        });
    }

    for(std::thread& worker : threads){
        worker.join();
    }

    std::vector<int> next_frontier;

    for(int thread_id = 0; thread_id < threadCount; thread_id++){
        for(int bank_id : local_frontiers[thread_id]){
            next_frontier.push_back(bank_id);
        }
    }

    return next_frontier;
}

void run_contagion_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, int numberOfThreads){
    LoanIndex loan_index = build_loan_index(loans, static_cast<int>(banks.size()));
    std::vector<char> already_propagated(banks.size(), false);
    std::vector<int> frontier = initial_defaulted_banks(banks);

    while(!frontier.empty()){
        propagate_losses_from_frontier_parallel(banks, loans, loan_index, frontier, numberOfThreads);

        for(int bank_id : frontier){
            already_propagated[bank_id] = true;
        }

        frontier = find_next_frontier_parallel(banks, loan_index, already_propagated, numberOfThreads);
    }
}
