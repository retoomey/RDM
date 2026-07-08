#pragma once
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include "IClient.h"
#include "RpcTypes.h"
#include "ProdClass.h"

namespace rdm {

bool_t xdr_net_mutable_product(XDR* xdrs, MutableProduct* mprod);
bool_t xdr_net_prod_info(XDR* xdrs, ProdInfo* info);
bool_t xdr_net_product(XDR* xdrs, Product* prod);
bool_t xdr_net_prod_spec(XDR* xdrs, ProdSpec* spec);
bool_t xdr_net_prod_class(XDR* xdrs, ProdClass* clss);
bool_t xdr_net_feedpar(XDR* xdrs, FeedParNet* fpar);
bool_t xdr_net_fornme_reply(XDR* xdrs, FeedResponse* reply);
bool_t xdr_net_hiya_reply(XDR* xdrs, HiyaResponse* reply);
bool_t xdr_net_comingsoon_args(XDR* xdrs, ComingSoonArgsNet* args);
bool_t xdr_net_datapkt(XDR* xdrs, DataPktNet* pkt);

}
