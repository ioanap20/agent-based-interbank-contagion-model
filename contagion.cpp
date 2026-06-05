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
#include <functional>
#include <condition_variable>
#include <string>
#include <iostream>

static const double recovery_rate = 0.4;

//Instead of creating new threads every time we want to parallelize a loop, this class creates the threads once, keeps them alive, and reuses them for different parallel loops because creating threads is expensive.

class ParallelForPool{
    private:
        std::vector<std::thread> workers;

        std::mutex mutex;
        std::condition_variable startCondition;
        std::condition_variable doneCondition;

        bool stopping = false;
        int generation = 0; // a new parallel job
        int activeWorkers = 0;

        int rangeBegin = 0;
        int rangeEnd = 0;
        int nextBegin = 0;
        int chunkSize = 1;

        std::function<void(int, int, int)> currentJob;
    
    public:
        ParallelForPool(int numberOfThreads){
            if(numberOfThreads < 1){
                numberOfThreads = 1;
            }

            workers.reserve(numberOfThreads);

            //create the worker threads
            for(int thread_id = 0; thread_id < numberOfThreads; thread_id++){
                workers.emplace_back([this, thread_id](){
                    int seenGeneration = 0;

                    while(true){
                        {
                        std::unique_lock<std::mutex> lock(mutex);
                        
                        //each one waits to recieve a job
                        //wait until either the pool is stopping or there is a new job
                        startCondition.wait(lock, [&](){
                            return stopping || generation != seenGeneration;
                        });

                        if(stopping){
                            return;
                        }

                        seenGeneration = generation;
                    }

                    while(true){
                        int begin;
                        int end;
                        {
                            std::lock_guard<std::mutex> lock(mutex);

                            if(nextBegin >= rangeEnd){
                                break;
                            }

                            //each worker takes a chunk
                            begin = nextBegin;
                            end = std::min(rangeEnd, begin + chunkSize);
                            nextBegin = end;
                        }

                        // then each worker does the job
                        currentJob(begin, end, thread_id);
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        activeWorkers--;

                        if(activeWorkers == 0){
                            //when all the workers are done, the last one notifies the main thread
                            doneCondition.notify_one();
                        }
                    }
                }

                });
            }
        }
        //destructor that is used when there is no more need for the pool, it tells the workers to stop
        ~ParallelForPool(){
            {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            generation++;
            }

            //wake up all threads
            startCondition.notify_all();

            //then join them
            for(std::thread& worker : workers){
                if(worker.joinable()){
                    worker.join();
                }
            }
        }

        int size() const{
            return workers.size();
        }

        //main function that runs the loop in parallel
        void parallel_for(int begin, int end, const std::function<void(int, int, int)>& job){
            //check that there is work
            if(end <= begin){
                return;
            }

            //store the information
            {
                std::lock_guard<std::mutex> lock(mutex);

                rangeBegin = begin;
                rangeEnd = end;
                nextBegin = begin;

                int totalWork= end - begin;
                int workerCount = static_cast<int>(workers.size());
                chunkSize = std::max(1, (totalWork + workerCount - 1) / workerCount);

                currentJob = job;
                activeWorkers = workers.size(); //all workers are active
                generation++; //new job
            }

            //take all the workers
            startCondition.notify_all();
            {
                std::unique_lock<std::mutex> lock(mutex);

                //the main thread waits until all workers are done
                doneCondition.wait(lock, [&](){
                    return activeWorkers == 0;
                });
            }
        }
};

//each thread produces its own losses. Will this create any difference between the seq and par alg?
static void apply_total_losses_from_threads(std::vector<Bank>& banks, const std::vector<std::vector<std::pair<int, double>>>& local_losses){
    std::vector<double> total_losses(banks.size(), 0.0);
    std::vector<char> touched(banks.size(), false);
    std::vector<int> touched_banks;

    for(const auto& thread_losses : local_losses){
        for(const auto& entry: thread_losses){
            int bank_id = entry.first;
            double loss = entry.second;

            if(!touched[bank_id]){
                touched[bank_id] = true;
                touched_banks.push_back(bank_id);
            }
            total_losses[bank_id] += loss;
        }
    }

    for(int bank_id : touched_banks){
        apply_loss(banks[bank_id], total_losses[bank_id]);

        if(is_insolvent(banks[bank_id])){
            banks[bank_id].defaulted = true;
        }
    }
}

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
            continue;;
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
    LoanIndex loan_index = build_loan_index(loans, banks.size());
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


static void propagate_losses_from_frontier_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, const LoanIndex& loan_index, const std::vector<int>& frontier, int numberOfThreads, ParallelForPool& pool){

    int numberOfActiveLoans = 0;

    for(int bank_id : frontier){
        numberOfActiveLoans += loan_index.by_borrower[bank_id].size(); // how many active loans are affected
    }

    if(numberOfActiveLoans == 0) return;

    std::vector<int> active_loans;
    active_loans.reserve(numberOfActiveLoans);

    for(int bank_id : frontier){
        for(int loan_idx : loan_index.by_borrower[bank_id]){
            active_loans.push_back(loan_idx);
        }
    }

    const int parallel_threshold = 2000; //check whether we need to do parallelisation or not

    if(numberOfActiveLoans < parallel_threshold){
        std::vector<std::pair<int, double>> losses;
        losses.reserve(active_loans.size());

        for(int loan_idx : active_loans){
            const Loan& loan = loans[loan_idx];
            double loss = (1.0 - recovery_rate) * loan.payment_due;
            losses.push_back({loan.lender, loss});
        }
        apply_total_losses(banks, losses);
        return;
    }

    int threadCount = pool.size();

    std::vector<std::vector<std::pair<int, double>>> local_losses(threadCount); // one local loss vector per thread

    for(int thread_id = 0; thread_id < threadCount; thread_id++){
        local_losses[thread_id].reserve(numberOfActiveLoans / threadCount + 1);
    }

    pool.parallel_for(0, numberOfActiveLoans, [&](int begin, int end, int thread_id){
        for(int i=begin; i<end; i++){
            int loan_idx = active_loans[i];
            const Loan& loan = loans[loan_idx];

            double loss = (1.0 - recovery_rate) * loan.payment_due;

            local_losses[thread_id].push_back({loan.lender, loss});
        }
    });
    apply_total_losses_from_threads(banks, local_losses);
}

//scan all banks and find which ones should be in the next frontier
static std::vector<int> find_next_frontier_parallel(std::vector<Bank>& banks, const LoanIndex& loan_index, const std::vector<char>& already_propagated, int numberOfThreads, ParallelForPool& pool){
    const int bank_parallel_threshold = 10000;

    if(banks.size() < bank_parallel_threshold) return find_next_frontier(banks, loan_index, already_propagated);

    int threadCount = pool.size();

    std::vector<std::vector<int>> local_frontiers(threadCount);

    for(int thread_id = 0; thread_id < threadCount; thread_id++){
        local_frontiers[thread_id].reserve(banks.size() / threadCount + 1);
    }

    pool.parallel_for(0, banks.size(), [&](int begin, int end, int thread_id){
        for(int bank_id = begin; bank_id < end; bank_id ++){
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

    std::vector<int> next_frontier;

    for(int thread_id = 0; thread_id < threadCount; thread_id ++){
        for(int bank_id : local_frontiers[thread_id]){
            next_frontier.push_back(bank_id);
        }
    }
    return next_frontier;
}

void run_contagion_parallel(std::vector<Bank>& banks, const std::vector<Loan>& loans, int numberOfThreads){
    /*int num_banks=static_cast<int>(banks.size());
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
    }*/

    LoanIndex loan_index = build_loan_index(loans, banks.size());
    std::vector<char> already_propagated(banks.size(), false);
    std::vector<int> frontier = initial_defaulted_banks(banks);

    int amountOfWork = std::max(banks.size(), loans.size());
    int threadCount = choose_thread_count(numberOfThreads, amountOfWork);

    ParallelForPool pool(threadCount);

    while(!frontier.empty()){
        propagate_losses_from_frontier_parallel(banks, loans, loan_index, frontier, numberOfThreads, pool);

        for(int bank_id : frontier){
            already_propagated[bank_id] = true;
        }

        frontier = find_next_frontier_parallel(banks, loan_index, already_propagated, numberOfThreads, pool);
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

        round++;

    }

        std::cout << "===================================================" << std::endl;

}
