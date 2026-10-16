#ifdef DNS_RECORD_TYPE
DNS_RECORD_TYPE(A,     resolve4,     queryA,     ARES_REC_TYPE_A)
DNS_RECORD_TYPE(AAAA,  resolve6,     queryAaaa,  ARES_REC_TYPE_AAAA)
DNS_RECORD_TYPE(ANY,   resolveAny,   queryAny,   ARES_REC_TYPE_ANY)
DNS_RECORD_TYPE(CAA,   resolveCaa,   queryCaa,   ARES_REC_TYPE_CAA)
DNS_RECORD_TYPE(CNAME, resolveCname, queryCname, ARES_REC_TYPE_CNAME)
DNS_RECORD_TYPE(MX,    resolveMx,    queryMx,    ARES_REC_TYPE_MX)
DNS_RECORD_TYPE(NAPTR, resolveNaptr, queryNaptr, ARES_REC_TYPE_NAPTR)
DNS_RECORD_TYPE(NS,    resolveNs,    queryNs,    ARES_REC_TYPE_NS)
DNS_RECORD_TYPE(PTR,   resolvePtr,   queryPtr,   ARES_REC_TYPE_PTR)
DNS_RECORD_TYPE(SOA,   resolveSoa,   querySoa,   ARES_REC_TYPE_SOA)
DNS_RECORD_TYPE(SRV,   resolveSrv,   querySrv,   ARES_REC_TYPE_SRV)
DNS_RECORD_TYPE(TLSA,  resolveTlsa,  queryTlsa,  ARES_REC_TYPE_TLSA)
DNS_RECORD_TYPE(TXT,   resolveTxt,   queryTxt,   ARES_REC_TYPE_TXT)
#undef DNS_RECORD_TYPE
#endif
