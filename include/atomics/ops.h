#ifdef ATOMICS_OP
ATOMICS_OP(add,             ADD,              3)
ATOMICS_OP(and,             AND,              3)
ATOMICS_OP(compareExchange, COMPARE_EXCHANGE, 4)
ATOMICS_OP(exchange,        EXCHANGE,         3)
ATOMICS_OP(load,            LOAD,             2)
ATOMICS_OP(or,              OR,               3)
ATOMICS_OP(store,           STORE,            3)
ATOMICS_OP(sub,             SUB,              3)
ATOMICS_OP(xor,             XOR,              3)
#undef ATOMICS_OP
#endif
