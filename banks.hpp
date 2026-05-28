enum class BankType{
    Small,
    Large
};

enum class BankRiskType{
    Fragile,
    Robust
};

enum class BankRole{
    Peripheral,
    Core
};

struct BalanceSheet{
    double assets = 0.0; //what people owe you ex: buildings, shares, trades etc...
        //anything that brings income in the future
    double liabilities = 0.0; //what you owe them
    
    double cash = 0.0; //actual money
    double otherAssets = 0.0;//actual money
    double otherLiabilities = 0.0; //other things besides banks

    double equity; //How much you add to your liabilities to reach your assets 
                    // it should be as big as possible
}

struct Bank{
    int id;
    BankType type;

    BankRiskTypes

    bool defaulted; // no more money - can not pay dept

    double targetOvernightLendingRatio; // lending such that the tables match at the end of the day that are being given back overnight
    double targetOvernightBorrowingRatio; // same but inversed
    double targetShortTermLendingRatio; // lending from the guvernment
    double targetShortTermBorrowingRatio; // borrowing from the guvernment
    double targetLongTermLendingRatio; // lending within banks by own initiative
    double targetLongTermBorrowingRatio; // the other way around

};

double compute_equity(const Bank& bank);
bool is_insolvent(const Bank& bank); // asstes < libilities 
bool is_illiquid(const Bank& bank, double payment_due); //checks if you have or not enough cash
void update_equity(Bank& bank);