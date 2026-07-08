The network folder will contain implementations of the
ip communication layer.

sunrpc --legacy ldm style rpc (but using system rpc now)

The possibly cool stuff.  If we can get the dividing API
layer clean enough, we should be able to implement different
network engines.  Some ideas below:

grpc -- google's rpc engine
cap'n proto -- another rpc engine
