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
#include <future>
#include <mutex>
#include <condition_variable>

struct SimpleThreadBarrier{
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
    bool global_converged = false;

    SimpleThreadBarrier calculation_barrier(threadCount+1);
    SimpleThreadBarrier evaluation_barrier(threadCount+1);

    std::vector<std::thread> workers;
    for(int t=0;t<threadCount; ++t){
        int begin=static_cast<int>(loans.size())*t/threadCount;
        int end=static_cast<int>(loans.size())*(t+1)/threadCount;

        workers.emplace_back([&, t, begin, end](){
            for(int iter=0; iter<max_iterations; ++iter){
                std::fill(local_incoming[t].begin(), local_incoming[t].end(), 0.0);

                for(int j=begin; j<end; ++j){
                    const auto& loan=loans[j];
                    local_incoming[t][loan.lender]+=loan.payment_due*repayment_ratio[loan.borrower];
                }

                calculation_barrier.wait();
                evaluation_barrier.wait();

                if(global_converged) break;
            }
        });
    }

    for(int iter=0; iter<max_iterations; ++iter){
        calculation_barrier.wait();

        std::fill(global_incoming_payments.begin(), global_incoming_payments.end(), 0.0);
        for(int t=0; t<threadCount; ++t){
            for(int i=0; i<num_banks; i++){
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

        if(max_difference<convergence_epsilon){
            global_converged=true;
        }

        evaluation_barrier.wait();
        if(global_converged) break;
    }

    for(auto& worker:workers){
        if(worker.joinable()) worker.join();
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
