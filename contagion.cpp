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
        bool useStaticPartition = false;

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
                        bool staticPartition = false;
                        int jobBegin = 0;
                        int jobEnd = 0;
                        int jobChunkSize = 1;

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
                        staticPartition = useStaticPartition;
                        jobBegin = rangeBegin;
                        jobEnd = rangeEnd;
                        jobChunkSize = chunkSize;
                        }

                    if(staticPartition){
                        int begin = jobBegin + thread_id * jobChunkSize;
                        int end = std::min(jobEnd, begin + jobChunkSize);
                        if(begin < end){
                            currentJob(begin, end, thread_id);
                        }
                    }else{
                        while(true){
                            int begin;
                            int end;
                            {
                                std::lock_guard<std::mutex> lock(mutex);

                                if(nextBegin >= jobEnd){
                                    break;
                                }

                                begin = nextBegin;
                                end = std::min(jobEnd, begin + jobChunkSize);
                                nextBegin = end;
                            }

                            currentJob(begin, end, thread_id);
                        }
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

        void run_job(int begin, int end, const std::function<void(int, int, int)>& job, bool staticPartition){
            if(end <= begin){
                return;
            }

            {
                std::lock_guard<std::mutex> lock(mutex);

                rangeBegin = begin;
                rangeEnd = end;
                nextBegin = begin;

                int totalWork = end - begin;
                int workerCount = static_cast<int>(workers.size());
                chunkSize = std::max(1, (totalWork + workerCount - 1) / workerCount);
                useStaticPartition = staticPartition;

                currentJob = job;
                activeWorkers = workerCount;
                generation++;
            }

            startCondition.notify_all();
            {
                std::unique_lock<std::mutex> lock(mutex);
                doneCondition.wait(lock, [&](){
                    return activeWorkers == 0;
                });
            }
        }

        void parallel_for(int begin, int end, const std::function<void(int, int, int)>& job){
            run_job(begin, end, job, false);
        }

        void parallel_for_static(int begin, int end, const std::function<void(int, int, int)>& job){
            run_job(begin, end, job, true);
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

struct RepaymentWorkspace{
    std::vector<std::vector<int>> loans_by_lender;
    std::vector<double> global_incoming;
    std::vector<double> global_credit_losses;
    std::vector<double> local_max_diff;

    void init(int threadCount, int numBanks, const std::vector<Loan>& loans){
        loans_by_lender.assign(numBanks, {});
        for(int loanIdx = 0; loanIdx < static_cast<int>(loans.size()); ++loanIdx){
            loans_by_lender[loans[loanIdx].lender].push_back(loanIdx);
        }

        global_incoming.assign(numBanks, 0.0);
        global_credit_losses.assign(numBanks, 0.0);
        local_max_diff.assign(threadCount, 0.0);
    }
};

static ParallelForPool& get_thread_pool(int threadCount){
    static std::unique_ptr<ParallelForPool> pool;
    static int pool_thread_count = 0;

    if(!pool || pool_thread_count != threadCount){
        pool = std::make_unique<ParallelForPool>(threadCount);
        pool_thread_count = threadCount;
    }

    return *pool;
}

static void parallel_accumulate_by_lender(ParallelForPool& pool,
                                          const std::vector<Loan>& loans,
                                          const std::vector<double>& repaymentRatio,
                                          RepaymentWorkspace& workspace,
                                          bool includeCreditLosses){
    int numBanks = static_cast<int>(workspace.global_incoming.size());

    pool.parallel_for_static(0, numBanks, [&](int begin, int end, int tid){
        (void)tid;
        for(int lender = begin; lender < end; ++lender){
            double incoming = 0.0;
            double creditLosses = 0.0;

            for(int loanIdx : workspace.loans_by_lender[lender]){
                const Loan& loan = loans[loanIdx];
                double ratio = repaymentRatio[loan.borrower];
                double payment = loan.payment_due * ratio;
                incoming += payment;
                if(includeCreditLosses){
                    creditLosses += loan.payment_due * (1.0 - ratio);
                }
            }

            workspace.global_incoming[lender] = incoming;
            if(includeCreditLosses){
                workspace.global_credit_losses[lender] = creditLosses;
            }
        }
    });
}

static double parallel_update_repayment_ratios(ParallelForPool& pool,
                                                int threadCount,
                                                std::vector<Bank>& banks,
                                                const std::vector<double>& nominalLiabilities,
                                                const std::vector<double>& globalIncoming,
                                                const std::vector<double>& repaymentRatio,
                                                std::vector<double>& nextRatio,
                                                std::vector<double>& localMaxDiff){
    int numBanks = static_cast<int>(banks.size());
    nextRatio = repaymentRatio;

    pool.parallel_for_static(0, numBanks, [&](int begin, int end, int tid){
        double threadMax = 0.0;

        for(int i = begin; i < end; ++i){
            if(nominalLiabilities[i] == 0.0){
                continue;
            }

            double availableFunds = banks[i].balanceSheet.cash + globalIncoming[i];
            double currentRatio = std::min(1.0, availableFunds / nominalLiabilities[i]);

            if(currentRatio < 0.0){
                currentRatio = 0.0;
            }

            double difference = std::abs(currentRatio - repaymentRatio[i]);
            if(difference > threadMax){
                threadMax = difference;
            }

            nextRatio[i] = currentRatio;
        }

        localMaxDiff[tid] = threadMax;
    });

    return *std::max_element(localMaxDiff.begin(), localMaxDiff.begin() + threadCount);
}

static std::vector<double> compute_nominal_liabilities(const std::vector<Loan>& loans, int numberOfBanks){
    std::vector<double> nominalLiabilities(numberOfBanks, 0.0);

    for(const auto& loan:loans){
        nominalLiabilities[loan.borrower]+=loan.payment_due;
    }

    return nominalLiabilities;
}

static void apply_repayment_result(std::vector<Bank>& banks, const std::vector<Loan>& loans, const std::vector<double>& nominalLiabilities,
                            const std::vector<double>& repaymentRatio, double convergenceEpsilon){
    int numberOfBanks=static_cast<int>(banks.size());  

    std::vector<double> totalIncoming(numberOfBanks, 0.0);
    std::vector<double> creditLosses(numberOfBanks, 0.0);

    for(const auto& loan:loans){
        totalIncoming[loan.lender]+=loan.payment_due*repaymentRatio[loan.borrower];
        creditLosses[loan.lender]+=loan.payment_due*(1.0-repaymentRatio[loan.borrower]);
    }

    for(int i=0; i<numberOfBanks; ++i){
        if(repaymentRatio[i]<1.0-convergenceEpsilon){
            banks[i].defaulted=true;
        }

        double totalOutgoing=nominalLiabilities[i]*repaymentRatio[i];
        banks[i].balanceSheet.cash+=(totalIncoming[i]-totalOutgoing);

        if(creditLosses[i]>0.0){
            apply_loss(banks[i], creditLosses[i]);
        }


        //Does it default if it is insolvent???
        if(is_insolvent(banks[i])){
            banks[i].defaulted = true;
        }
    }
}

static void apply_repayment_result_parallel(ParallelForPool& pool,
                                            int threadCount,
                                            std::vector<Bank>& banks,
                                            const std::vector<Loan>& loans,
                                            const std::vector<double>& nominalLiabilities,
                                            const std::vector<double>& repaymentRatio,
                                            double convergenceEpsilon,
                                            RepaymentWorkspace& workspace){
    (void)threadCount;
    parallel_accumulate_by_lender(pool, loans, repaymentRatio, workspace, true);

    int numBanks = static_cast<int>(banks.size());

    pool.parallel_for_static(0, numBanks, [&](int begin, int end, int tid){
        (void)tid;
        for(int i = begin; i < end; ++i){
            if(repaymentRatio[i] < 1.0 - convergenceEpsilon){
                banks[i].defaulted = true;
            }

            double totalOutgoing = nominalLiabilities[i] * repaymentRatio[i];
            banks[i].balanceSheet.cash += (workspace.global_incoming[i] - totalOutgoing);

            if(workspace.global_credit_losses[i] > 0.0){
                apply_loss(banks[i], workspace.global_credit_losses[i]);
            }

            if(is_insolvent(banks[i])){
                banks[i].defaulted = true;
            }
        }
    });
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

    std::vector<double> repayment_ratio(numberOfBanks, 1.0);

    const int maxIterations=100;
    const double convergenceEpsilon=1e-6;

    for(int iter=0; iter<maxIterations; ++iter){
        std::vector<double> incomingPayments(numberOfBanks, 0.0);

        for(const auto& loan:loans){
            incomingPayments[loan.lender]+=loan.payment_due*repayment_ratio[loan.borrower];
        }

        std::vector<double> next_ratio=repayment_ratio;
        double maxDifference=0.0;

        for(int i=0; i<numberOfBanks; i++){
            if(nominalLiabilities[i]==0.0) continue;
            double available_funds = banks[i].balanceSheet.cash + incomingPayments[i];
            double current_ratio = std::min(1.0, available_funds/nominalLiabilities[i]);

            if(current_ratio<0.0) current_ratio=0.0;

            double difference = std::abs(current_ratio-repayment_ratio[i]);

            if(difference>maxDifference){
                maxDifference=difference;
            }
            next_ratio[i]=current_ratio;
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
    if(static_cast<int>(loans.size()) < repayment_parallel_threshold){
        run_contagion(banks, loans);
        return;
    }

    int num_banks = static_cast<int>(banks.size());
    int threadCount = choose_thread_count(numberOfThreads, static_cast<int>(loans.size()));

    ParallelForPool& pool = get_thread_pool(threadCount);

    RepaymentWorkspace workspace;
    workspace.init(threadCount, num_banks, loans);

    std::vector<double> nominal_liabilities = compute_nominal_liabilities(loans, num_banks);
    std::vector<double> repayment_ratio(num_banks, 1.0);
    std::vector<double> next_ratio(num_banks, 1.0);

    const int max_iterations = 100;
    const double convergence_epsilon = 1e-6;

    for(int iteration = 0; iteration < max_iterations; ++iteration){
        parallel_accumulate_by_lender(pool, loans, repayment_ratio, workspace, false);

        double max_difference = parallel_update_repayment_ratios(
            pool,
            threadCount,
            banks,
            nominal_liabilities,
            workspace.global_incoming,
            repayment_ratio,
            next_ratio,
            workspace.local_max_diff
        );

        repayment_ratio.swap(next_ratio);

        if(max_difference < convergence_epsilon){
            break;
        }
    }

    apply_repayment_result_parallel(
        pool,
        threadCount,
        banks,
        loans,
        nominal_liabilities,
        repayment_ratio,
        convergence_epsilon,
        workspace
    );
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
