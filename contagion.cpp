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
#include "banks.hpp"

#include <vector>
#include <thread>
#include <algorithm>
#include <utility>
<<<<<<< HEAD
#include <iostream>
#include <iomanip>

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
=======
#include <future>
>>>>>>> origin/Nadia

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

void run_contagion(std::vector<Bank>& banks, const std::vector<Loan>& loans){
    int num_banks=static_cast<int>(banks.size());

    std::vector<double> nominal_liabilities(num_banks, 0.0);
    for(const auto& loan:loans){
        nominal_liabilities[loan.borrower]+=loan.payment_due;
    }

    std::vector<double> repayment_ratio(num_banks, 1.0);
    const int max_iterations=100;
    const double convergence_epsilon=1e-6;

    for(int iter=0; iter<max_iterations; ++iter){
        std::vector<double> next_ratio=repayment_ratio;
        std::vector<double> incoming_payments(num_banks, 0.0);

        for(const auto& loan:loans){
            incoming_payments[loan.lender]+=loan.payment_due*repayment_ratio[loan.borrower];
        }

        double max_difference=0.0;

        for(int i=0; i<num_banks; i++){
            if(nominal_liabilities[i]==0.0) continue;

            double available_funds = banks[i].balanceSheet.cash + incoming_payments[i];
            double current_ratio = std::min(1.0, available_funds/nominal_liabilities[i]);
            if(current_ratio<0.0) current_ratio=0.0;

            double difference = std::abs(current_ratio-repayment_ratio[i]);
            if(difference>max_difference){
                max_difference=difference;
            }
            next_ratio[i]=current_ratio;
        }

        repayment_ratio=next_ratio;

        if(max_difference<convergence_epsilon) break;
    }

    std::vector<double> total_incoming(num_banks, 0.0);
    std::vector<double> credit_losses(num_banks, 0.0);

    for(const auto& loan:loans){
        total_incoming[loan.lender]+=loan.payment_due*repayment_ratio[loan.borrower];
        credit_losses[loan.lender]+=loan.payment_due*(1.0-repayment_ratio[loan.borrower]);
    }

    for(int i=0; i<num_banks; ++i){
        if(repayment_ratio[i]<1.0-convergence_epsilon){
            banks[i].defaulted=true;
        }

        double total_outgoing=nominal_liabilities[i]*repayment_ratio[i];
        banks[i].balanceSheet.cash+=(total_incoming[i]-total_outgoing);

        if(credit_losses[i]>0.0){
            apply_loss(banks[i], credit_losses[i]);
        }
    }
}

void run_contagion_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, int numberOfThreads){
    int num_banks=static_cast<int>(banks.size());
    int threadCount=choose_thread_count(numberOfThreads, static_cast<int>(loans.size()));

    std::vector<double> nominal_liabilities(num_banks, 0.0);
    for(const auto& loan:loans){
        nominal_liabilities[loan.borrower]+=loan.payment_due;
    }

    std::vector<double> repayment_ratio(num_banks, 1.0);
    const int max_iterations=100;
    const double convergence_epsilon=1e-6;

    std::vector<std::vector<double>> local_incoming(threadCount, std::vector<double>(num_banks, 0.0));
    std::vector<double> global_incoming_payments(num_banks,0.0);

    for(int iter=0;iter<max_iterations; ++iter){

        for(int t=0; t<threadCount; ++t){
            std::fill(local_incoming[t].begin(), local_incoming[t].end(), 0.0);
        }
        
        std::vector<std::future<void>> futures;

        for(int t=0; t<threadCount; ++t){
            int begin=static_cast<int>(loans.size())*t/threadCount;
            int end=static_cast<int>(loans.size())*(t+1)/threadCount;

            futures.push_back(std::async(std::launch::async, [&, t, begin, end](){
                for(int j=begin; j<end; ++j){
                    const auto& loan = loans[j];
                    local_incoming[t][loan.lender]+=loan.payment_due*repayment_ratio[loan.borrower];
                }
            }));
        }

        for(auto& fut:futures){
            fut.get();
        }

        std::vector<double> global_incoming_payments(num_banks, 0.0);
        for(int t=0; t<threadCount; ++t){
            for(int i=0; i<num_banks; ++i){
                global_incoming_payments[i]+=local_incoming[t][i];
            }
        }

        std::vector<double> next_ratio=repayment_ratio;
        double max_difference=0.0;

        for(int i=0; i<num_banks; ++i){
            if(nominal_liabilities[i]==0.0) continue;

            double available_funds=banks[i].balanceSheet.cash+global_incoming_payments[i];
            double current_ratio=std::min(1.0, available_funds/nominal_liabilities[i]);
            if(current_ratio<0.0) current_ratio=0.0;

            double difference=std::abs(current_ratio-repayment_ratio[i]);
            if(difference>max_difference){
                max_difference=difference;
            }
            next_ratio[i]=current_ratio;
        }

        repayment_ratio=next_ratio;
        if(max_difference<convergence_epsilon) break;
    }

    std::vector<double> total_incoming(num_banks, 0.0);
    std::vector<double> credit_losses(num_banks, 0.0);

    for(const auto& loan:loans){
        total_incoming[loan.lender]+=loan.payment_due*repayment_ratio[loan.borrower];
        credit_losses[loan.lender]+=loan.payment_due*(1.0-repayment_ratio[loan.borrower]);
    }

    for(int i=0; i<num_banks;++i){
        if(repayment_ratio[i]<1.0-convergence_epsilon){
            banks[i].defaulted=true;
        }
        double total_outgoing=nominal_liabilities[i]*repayment_ratio[i];
        banks[i].balanceSheet.cash+=(total_incoming[i]-total_outgoing);

        if(credit_losses[i]>0.0){
            apply_loss(banks[i], credit_losses[i]);
        }
    }
}

static void print_frontier_small(const std::vector<int>& frontier){
    std::cout<<"Banks defaulting this round: ";

    for(int bank_id : frontier){
        std::cout << bank_id << " ";
    }
    std::cout << std::endl;
}

static void print_losses_small(const std::vector<std::pair<int, double>>& losses){
    if(losses.empty()){
        std::cout<<"No lender receives losses this round" << std::endl;
        return;
    }

    std::cout<<"Losses applied this round:" << std::endl;

    for(const auto& lossEntry : losses){
        std::cout <<" Bank "<< lossEntry.first<<" losses "<<std::fixed<< std::setprecision(2)
                  << lossEntry.second
                  << std::endl;
    }
}

void run_contagion_small(std::vector<Bank>& banks, const std::vector<Loan>& loans){
    LoanIndex loan_index = build_loan_index(loans, banks.size());

    std::vector<char> already_propagated(banks.size(), false);
    std::vector<int> frontier = initial_defaulted_banks(banks);

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

                double loss = (1.0 - recovery_rate) * loan.payment_due;

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
            already_propagated[bank_id] = true;
        }

        frontier = find_next_frontier(banks, loan_index, already_propagated);

        if(frontier.empty()){
            std::cout << std::endl;
            std::cout << "No new banks defaulted. The system is now stable." << std::endl;
        }
        round ++;
    }
    
    std::cout << "===================================================" << std::endl;

}