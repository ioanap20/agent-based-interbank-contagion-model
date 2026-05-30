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

 //------------ Sequential ------------
 #include "contagion.hpp"

 #include <vector>
 #include <thread>
 #include <future>
 #include <algorithm>

 static const double recovery_rate = 0.4;

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

    for(int i=0; i < banks.size(); i++){
        if(banks[i].defaulted){
            frontier.push_back(i);
        }
    }

    return frontier;
 }

 static void propagate_losses_from_frontier(std::vector<Bank>& banks, const std::vector<Loan>& loans, const std::vector<int>& frontier){
    std::vector<char> is_in_frontier(banks.size(), false);

    for(int bank_id : frontier){
        is_in_frontier[bank_id] = true;
    }

    for(const Loan& loan : loans){
        int borrower_id = loan.borrower;
        int lender_id = loan.lender;

        if(is_in_frontier[borrower_id]){
            double loss = (1.0 - recovery_rate) * loan.payment_due;

            apply_loss(banks[lender_id], loss);

            if(is_insolvent(banks[lender_id])){
                banks[lender_id].defaulted = true;
            }
        }
    }
 }

 static std::vector<int> find_next_frontier(std::vector<Bank>& banks, const std::vector<Loan>& loans, const std::vector<char>& alreadt_propagated){
    std::vector<int> next_frontier;
    
    for(int i=0; i<banks.size(); i++){
        if(alreadt_propagated[i]) continue;

        if(banks[i].defaulted){
            next_frontier.push_back(i);
            continue;
        }

        double outgoing_payment = total_outgoing_payment(i, loans);
        double incoming_payment = total_incoming_payment(i, loans);

        bool insolvent = is_insolvent(banks[i]);
        bool illiquid = is_illiquid(banks[i], outgoing_payment, incoming_payment);

        if(insolvent || illiquid){
            banks[i].defaulted = true;
            next_frontier.push_back(i);
        }
    }
    return next_frontier;
 } 

 /*void propagate_losses(std::vector<Bank>& banks, const std::vector<Loan>& loans){
    for(const Loan& loan : loans){
        int borrower_id = loan.borrower;
        int lender_id = loan.lender;

        if(banks[borrower_id].defaulted){
            double loss = (1.0 - recovery_rate) * loan.payment_due;

            apply_loss(banks[lender_id], loss);
        }
    }
 }

 void */

 /*void run_contagion(std::vector<Bank>& banks, const std::vector<Loan>& loans){
    bool new_default = true;

    while(new_default){
        new_default = false;

        for(int i=0; i<banks.size(); i++){
            if(banks[i].defaulted){
                continue;
            }

            double outgoing_payment = total_outgoing_payment(i, loans);
            double incoming_payment = total_incoming_payment(i, loans);

            bool insolvent = is_insolvent(banks[i]);
            bool illiquid = is_illiquid(banks[i], outgoing_payment, incoming_payment);

            if(insolvent || illiquid){
                banks[i].defaulted = true;
                new_default = true;
            }
        }

        if(new_default){
            propagate_losses(banks, loans);
        }
    }
 }*/

 void run_contagion(std::vector<Bank>& banks, const std::vector<Loan>& loans){
    std::vector<char> already_propagated(banks.size(), false);

    std::vector<int>frontier = initial_defaulted_banks(banks);

    while(!frontier.empty()){
        propagate_losses_from_frontier(banks, loans, frontier);

        for(int bank_id : frontier){
            already_propagated[bank_id] = true;
        }

        frontier = find_next_frontier(banks, loans, already_propagated);
    }
 }


 //------------- Parallel -----------------

static void propagate_losses_from_frontier_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, const std::vector<int>& frontier, int numberOfThreads){
    std::vector<char> is_in_frontier(banks.size(), false);

    for(int bank_id : frontier){
        is_in_frontier[bank_id] = true;
    }

    int threadCount = choose_thread_count(numberOfThreads, loans.size());

    std::vector<std::vector<double>> loacal_losses(threadCount, std::vector<double>(banks.size(), 0.0));

    std::vector<std::future<void>> futures;

    for(int thread_id = 0; thread_id < threadCount; thread_id ++){
        int begin = loans.size() * thread_id / threadCount;
        int end = loans.size() * (thread_id + 1) / threadCount;

        futures.push_back(std::async(std::launch::async, [&, thread_id, begin, end](){
            for(int j = begin; j< end; j++){
                const Loan& loan = loans[j];

                int borrower_id = loan.borrower;
                int lender_id = loan.lender;

                if(is_in_frontier[borrower_id]){
                    double loss = (1.0 - recovery_rate) * loan.payment_due;
                    loacal_losses[thread_id][lender_id] += loss;
                }
            }
        }));
    }

    for(std:: future<void>& future : futures){
        future.get();
    }

    std::vector<double> total_losses(banks.size(), 0.0);

    for(int thread_id = 0; thread_id < threadCount; thread_id ++){
        for(int bank_id = 0; bank_id < banks.size(); bank_id++){
            total_losses[bank_id] += loacal_losses[thread_id][bank_id];
        }
    }

    for(int bank_id = 0; bank_id < banks.size(); bank_id ++){
        if(total_losses[bank_id] > 0.0){
            apply_loss(banks[bank_id], total_losses[bank_id]);

            if(is_insolvent(banks[bank_id])){
                banks[bank_id].defaulted = true;
            }
        }
    }
 }

 static std::vector<int> find_next_frontier_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, const std::vector<char>& alreadt_propagated, int numberOfThreads){
    
    int threadCount = choose_thread_count(numberOfThreads, banks.size());
    std::vector<std::vector<int>> local_frontiers(threadCount);

    std::vector<std::future<void>> futures;

    for(int thread_id = 0; thread_id < threadCount; thread_id ++){
        int begin = loans.size() * thread_id / threadCount;
        int end = loans.size() * (thread_id + 1) / threadCount;

        futures.push_back(std::async(std::launch::async, [&, thread_id, begin, end](){
            for(int j = begin; j< end; j++){
                if(alreadt_propagated[j]) continue;

                if(banks[j].defaulted){
                    local_frontiers[thread_id].push_back(j);
                    continue;
                }

                double outgoing_payment = total_outgoing_payment(j, loans);
                double incoming_payment = total_incoming_payment(j, loans);

                bool insolvent = is_insolvent(banks[j]);
                bool illiquid = is_illiquid(banks[j], outgoing_payment, incoming_payment);

                if(insolvent || illiquid){
                    banks[j].defaulted = true;
                    local_frontiers[thread_id].push_back(j);
                }

            }
        }));
    }
        for(std:: future<void>& future : futures){
            future.get();
        }
        std::vector<int> next_frontier;

        for(int thread_id = 0; thread_id < threadCount; thread_id ++){
            for(int bank_id : local_frontiers[thread_id]){
                next_frontier.push_back(bank_id);
            }
    }
    return next_frontier;
} 

void run_contagion_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, int numberOfThreads){
    std::vector<char> already_propagated(banks.size(), false);

    std::vector<int>frontier = initial_defaulted_banks(banks);

    while(!frontier.empty()){
        propagate_losses_from_frontier_parallel(banks, loans, frontier, numberOfThreads);

        for(int bank_id : frontier){
            already_propagated[bank_id] = true;
        }

        frontier = find_next_frontier_parallel(banks, loans, already_propagated, numberOfThreads);
    }
}

