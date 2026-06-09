# agent-based-interbank-contagion-model

We implement an agent-based model of contagion in an interbank financial network. Each bank
is represented as an individual agent with its own balance sheet, and banks are connected through
lending and borrowing relationships. When an external shock causes one or more banks to default,
distress propagates through the network: creditors receive only partial repayment from distressed
borrowers, and the resulting credit losses may reduce creditors’ capital and trigger further defaults.
Rather than propagating defaults round-by-round, our benchmark solver computes a repayment
fixed point. Each borrower is assigned a repayment ratio in [0, 1]; the solver iterates until these ratios
stabilize, then applies the resulting payments, credit losses, and default flags. We first implement
this algorithm sequentially, then develop a parallel version using a persistent worker team and a
lender-based work partitioning strategy, so that incoming-payment accumulation can be distributed
across threads without unsafe concurrent writes to the same creditor.
Our benchmarking method separates the one-time cost of constructing and preparing the
interbank network which includes four quarters of market formation, bank generation, and other
data structures used for the repeated cost of solving the contagion cascade. To make comparisons
fair, we use seeded random experiments, ensuring that different thread counts are tested on identical
generated markets and shocks. The parallel solver is checked against the sequential reference by
matching default counts on every run.