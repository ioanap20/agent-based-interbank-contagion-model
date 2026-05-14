/*
 * benchmarking.cpp
 *
 * Systematic performance experiments for the project.
 *
 * Different parameter regimes 
 *  - sequential and parallel implementations,
 *  - different numbers of threads,
 *  - different numbers of banks,
 *  - different interbank network densities,
 *  - different shock intensities or bank compositions.
 *
 * Records:
 *  - execution time,
 *  - number of defaulted banks,
 *  - size and depth of the contagion cascade,
 *  - speedup obtained from parallelization.
 *
 */